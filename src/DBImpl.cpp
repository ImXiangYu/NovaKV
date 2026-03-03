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
      recovery_loader_(db_path_, manifest_manager_, levels_) {
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

  LOG_INFO(std::string("SSTs & WALs Recovery complete. Items in memory: ") +
           std::to_string(mem_->Count()));
}

DBImpl::~DBImpl() {
  // 析构前最后落盘一次，保证数据不丢
  if (mem_->Count() > 0) {
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
    if (!compaction_engine_.PrepareMinor(mem_, imm_, active_wal_id_, ctx)) {
      LOG_ERROR("DBImpl::MinorCompaction aborted: PrepareMinor failed.");
      return;
    }
  }

  SSTableReader* reader = compaction_engine_.BuildMinorSST(ctx);
  if (reader == nullptr) {
    std::unique_lock state_lock(state_mu_);
    // Prepare 成功后会设置 imm_=mem_；若 Build 失败需要回滚，避免双指针悬挂
    if (imm_ == ctx.flushing_imm && mem_ == ctx.flushing_imm) {
      imm_ = nullptr;
    }
    LOG_ERROR("DBImpl::MinorCompaction aborted: BuildMinorSST failed.");
    return;
  }

  bool need_l0_compact = false;
  bool install_ok = false;
  {
    std::unique_lock state_lock(state_mu_);
    install_ok = compaction_engine_.InstallMinor(mem_, imm_, active_wal_id_,
                                                 ctx, reader, need_l0_compact);
    if (!install_ok) {
      // Install 失败时 reader 仍由调用方清理
      delete reader;
      if (imm_ == ctx.flushing_imm && mem_ == ctx.flushing_imm) {
        imm_ = nullptr;
      }
      LOG_ERROR("DBImpl::MinorCompaction aborted: InstallMinor failed.");
      return;
    }
  }

  if (need_l0_compact) {
    CompactL0ToL1();
  }
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

  bool need_minor_compaction = false;
  {
    std::unique_lock state_lock(state_mu_);
    // 1. 检查当前 MemTable 是否已满 (假设阈值为 1000 条)
    if (mem_->Count() >= 1000) {
      if (imm_ != nullptr) {
        // 如果上一个 imm_ 还没处理完，为了简单起见，这里先同步等待
        // 后期我们会用后台线程来优化这里
        LOG_WARN("Wait: MinorCompaction is too slow...");
      }
      need_minor_compaction = true;
    }
  }
  // 把耗时的Compaction放在锁外，避免锁的等待时间过长
  if (need_minor_compaction) {
    MinorCompaction();
  }

  // 2. 正常写入
  mem_->Put(key, value);
}
