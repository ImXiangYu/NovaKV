//
// Created by 26708 on 2026/2/7.
//

#include "DBImpl.h"

#include <filesystem>
#include <map>
#include <stdexcept>
#include <utility>

#include "Logger.h"

namespace fs = std::filesystem;

DBImpl::DBImpl(std::string db_path)
    : db_path_(std::move(db_path)),
      manifest_manager_(db_path_),
      levels_(2),
      compaction_engine_(db_path_, manifest_manager_, levels_),
      recovery_loader_(db_path_, manifest_manager_, levels_),
      bg_stopped_(false),
      bg_compaction_scheduled_(false) {
  // 1. 确保工作目录存在
  if (!fs::exists(db_path_)) {
    fs::create_directories(db_path_);
  }
  LOG_INFO(std::string("DB path: ") + db_path_);

  if (!manifest_manager_.Load()) {
    recovery_loader_.InitNextFileNumberFromDisk();
    manifest_manager_.Persist();
  }

  if (!manifest_manager_.ReplayLog()) {
    throw std::runtime_error("ReplayManifestLog failed");
  }
  recovery_loader_.LoadSSTables();

  // 3. 初始化第一个活跃的 MemTable
  // 每一个 MemTable 对应一个独立的日志文件
  const uint64_t new_wal_id = manifest_manager_.AllocateFileNumber();
  active_wal_id_ = new_wal_id;
  const std::string wal_path =
      db_path_ + "/" + std::to_string(new_wal_id) + ".wal";
  mem_ = new MemTable(wal_path);
  manifest_manager_.AddWal(new_wal_id);

  recovery_loader_.RecoverFromWals(mem_);

  // 构造函数最后启动后台进程
  background_thread_ = std::thread(&DBImpl::BackgroundLoop, this);

  LOG_INFO(std::string("SSTs & WALs Recovery complete. Items in memory: ") +
           std::to_string(mem_->Count()));
}

DBImpl::~DBImpl() {
  // 先停掉后台线程
  {
    std::unique_lock state_lock(state_mu_);
    bg_stopped_ = true;
    bg_cv_.notify_all();
  }
  if (background_thread_.joinable()) {
    background_thread_.join();
  }

  // 析构前最后落盘一次，保证数据不丢
  if (mem_ != nullptr && mem_->Count() > 0) {
    // 此时已经是单线程了，直接手动把 mem 换给 imm 调一次 MinorCompaction 即可
    imm_ = mem_;
    imm_wal_id_ = active_wal_id_;
    mem_ = nullptr;
    MinorCompaction();
  }

  for (auto& level : levels_) {
    for (const auto& reader : level) {
      delete reader;
    }
    level.clear();
  }

  // 如果imm_非空，也清理掉
  delete imm_;

  // 清理mem_
  delete mem_;
}

void DBImpl::MinorCompaction() {
  CompactionEngine::MinorCtx ctx;
  {
    std::unique_lock state_lock(state_mu_);

    // if (imm_ == nullptr) return;  // imm是空的，无需执行MinorCompaction

    ctx.flushing_imm = imm_;
    ctx.new_sst_id = manifest_manager_.AllocateFileNumber();
    ctx.new_sst_path = db_path_ + "/" + std::to_string(ctx.new_sst_id) + ".sst";

    ctx.old_wal_id = imm_wal_id_;
    ctx.old_wal_path = db_path_ + "/" + std::to_string(ctx.old_wal_id) + ".wal";
  }

  SSTableReader* reader = compaction_engine_.BuildMinorSST(ctx);

  bool trigger_l0_to_l1 = false;
  if (reader != nullptr) {
    std::unique_lock state_lock(state_mu_);

    // 更新磁盘元数据 (拿锁)
    levels_[0].push_back(reader);
    manifest_manager_.AddSst(ctx.new_sst_id, 0);

    // 清理：删掉旧 WAL，删掉旧内存
    manifest_manager_.RemoveWal(ctx.old_wal_id);
    std::filesystem::remove(ctx.old_wal_path);

    delete imm_;
    imm_ = nullptr;
    imm_wal_id_ = 0;

    LOG_INFO("Background Minor Compaction success.");

    // 检查是否触发 L0->L1
    if (levels_[0].size() >= 2) {
      // CompactL0ToL1();
      trigger_l0_to_l1 = true;
    }
  } else {
    LOG_ERROR("Background Minor Compaction failed to build SST.");
  }

  if (trigger_l0_to_l1) {
    // 触发 L0->L1
    CompactL0ToL1();
  }
}
void DBImpl::BackgroundLoop() {
  while (true) {
    std::unique_lock state_lock(state_mu_);
    bg_cv_.wait(state_lock,
                [this] { return bg_stopped_ || bg_compaction_scheduled_; });

    if (bg_stopped_) break;

    // 此时拿到了锁，且 bg_compaction_scheduled_ 为 true
    // 既然已经拿到锁了，我们可以执行 MinorCompaction
    state_lock.unlock();  // 先放锁，让 MinorCompaction 内部自己控锁
    MinorCompaction();
    state_lock.lock();  // 干完活再拿回锁，重置状态

    bg_compaction_scheduled_ = false;
    bg_cv_.notify_all();  // 通知前台Compaction完成
  }
  // 醒来后提醒
  LOG_INFO("Background compaction triggered");
}

void DBImpl::CompactL0ToL1() {
  CompactionEngine::L0ToL1Ctx ctx;
  {
    std::unique_lock state_lock(state_mu_);
    if (!compaction_engine_.PrepareL0ToL1(ctx)) {
      return;
    }
  }

  SSTableReader* reader = nullptr;
  if (ctx.has_output) {
    reader = compaction_engine_.BuildL0ToL1SST(ctx);
    if (reader == nullptr) {
      LOG_ERROR("DBImpl::CompactL0ToL1 aborted: BuildL0ToL1SST failed.");
      return;
    }
  }

  {
    std::unique_lock state_lock(state_mu_);
    if (!compaction_engine_.InstallL0ToL1(ctx, reader)) {
      if (reader != nullptr) {
        delete reader;
      }
      if (!ctx.new_sst_path.empty()) {
        fs::remove(ctx.new_sst_path);
      }
      LOG_ERROR("DBImpl::CompactL0ToL1 aborted: InstallL0ToL1 failed.");
    }
  }
}

size_t DBImpl::LevelSize(const size_t level) const {
  if (level >= levels_.size()) return 0;
  return levels_[level].size();
}

std::unique_ptr<DBIterator> DBImpl::NewIterator() {
  std::shared_lock state_lock(state_mu_);
  std::vector<std::pair<std::string, std::string> > rows;
  std::map<std::string, ValueRecord> seen;
  // 同 key 只保留最新版本，先从mem开始
  // 最新是 kDeletion 就不放入 rows_
  // 最后按 key 升序生成 rows_
  if (mem_) {
    auto s = mem_->Snapshot();
    for (auto& [k, rec] : s) seen.try_emplace(k, rec);
  }
  if (imm_) {
    auto s = imm_->Snapshot();
    for (auto& [k, rec] : s) seen.try_emplace(k, rec);
  }

  // 之后是L0，从新到旧，要逆序遍历
  for (auto l = levels_[0].rbegin(); l != levels_[0].rend(); ++l) {
    (*l)->ForEach([&](const std::string& key, const std::string& value,
                      const ValueType type) {
      seen.try_emplace(key, ValueRecord{type, value});
    });
  }

  // L1 同上
  for (auto l = levels_[1].rbegin(); l != levels_[1].rend(); ++l) {
    (*l)->ForEach([&](const std::string& key, const std::string& value,
                      const ValueType type) {
      seen.try_emplace(key, ValueRecord{type, value});
    });
  }

  for (const auto& [fst, snd] : seen) {
    if (snd.type == ValueType::kValue) {
      rows.emplace_back(fst, snd.value);
    }
  }

  return std::make_unique<DBIterator>(std::move(rows));
}
void DBImpl::Sync() {
  std::unique_lock state_lock(state_mu_);
  bg_cv_.wait(state_lock, [this] {
    return imm_ == nullptr && !bg_compaction_scheduled_;
  });
}

bool DBImpl::Get(const std::string& key, ValueRecord& value) const {
  std::shared_lock lock(state_mu_);
  // 第一级：查找活跃内存 (MemTable)
  if (mem_ && mem_->Get(key, value)) {
    // 如果是kValue，返回true
    // 如果是kDeletion，返回false
    if (value.type == ValueType::kValue) {
      LOG_DEBUG(std::string("Get hit: memtable key=") + key);
      return true;
    }
    return false;
  }

  // 第二级：查找只读内存 (Immutable MemTable)
  // 注意：如果 MinorCompaction 正在进行，imm_ 里的数据也是最新的
  if (imm_ && imm_->Get(key, value)) {
    if (value.type == ValueType::kValue) {
      LOG_DEBUG(std::string("Get hit: immutable memtable key=") + key);
      return true;
    }
    return false;
  }

  // 第三级：查找磁盘 SSTable (从新到旧)
  // 越晚生成的 SST 文件，数据越新，所以要逆序遍历
  // 先倒序遍历 levels_[0]（L0 新到旧）
  for (size_t i = levels_[0].size(); i-- > 0;) {
    ValueRecord rec{ValueType::kDeletion, ""};
    if (levels_[0][i]->GetRecord(key, &rec)) {
      if (rec.type == ValueType::kDeletion) return false;
      value = rec;
      return true;
    }
  }

  for (size_t i = levels_[1].size(); i-- > 0;) {
    ValueRecord rec{ValueType::kDeletion, ""};
    if (levels_[1][i]->GetRecord(key, &rec)) {
      if (rec.type == ValueType::kDeletion) return false;
      value = rec;
      return true;
    }
  }

  return false;
}

void DBImpl::Put(const std::string& key, const ValueRecord& value) {
  std::lock_guard write_lock(write_mu_);

  {
    std::unique_lock state_lock(state_mu_);
    // 1. 检查当前 MemTable 是否已满 (假设阈值为 1000 条)
    while (mem_->Count() >= 1000) {
      if (imm_ != nullptr) {
        bg_cv_.wait(state_lock);
      } else {
        // 此时 imm_ 为空，我们可以安全地切换
        imm_wal_id_ = active_wal_id_;
        imm_ = mem_;
        // 创建新 WAL 和新 MemTable (这部分很快，可以在锁内做)
        uint64_t new_wal_id = manifest_manager_.AllocateFileNumber();
        std::string new_wal =
            db_path_ + "/" + std::to_string(new_wal_id) + ".wal";
        mem_ = new MemTable(new_wal);
        active_wal_id_ = new_wal_id;
        manifest_manager_.AddWal(new_wal_id);

        // 唤醒后台
        bg_compaction_scheduled_ = true;
        bg_cv_.notify_all();
        break;
      }
    }
  }

  // 2. 正常写入
  mem_->Put(key, value);
}
