//
// Created by 26708 on 2026/2/7.
//

#include "DBImpl.h"
#include "Logger.h"
#include <filesystem>
#include <map>
#include <utility>
#include <stdexcept>

namespace fs = std::filesystem;

DBImpl::DBImpl(std::string db_path)
    : db_path_(std::move(db_path)),
      compaction_engine_(db_path_),
      manifest_manager_(db_path_),
      recovery_loader_(db_path_),
      imm_(nullptr) {
    // 1. 确保工作目录存在
    if (!fs::exists(db_path_)) {
        fs::create_directories(db_path_);
    }
    LOG_INFO(std::string("DB path: ") + db_path_);

    // 2. 初始化levels_
    // 先初始化为两层，即levels_[0] 是 L0，levels_[1] 是 L1
    levels_.resize(2);

    // 先尝试从manifest中获取state
    if (!LoadManifestState()) {
        // 如果失败，则读磁盘获取
        InitNextFileNumberFromDisk();
        // 获取后再写入
        PersistManifestState();
    }

    // 回放Log更新manifest_state_
    if (!ReplayManifestLog()) {
        throw std::runtime_error("ReplayManifestLog failed");
    }
    // 调用LoadSSTables()恢复数据
    LoadSSTables();

    // 3. 初始化第一个活跃的 MemTable
    // 每一个 MemTable 对应一个独立的日志文件
    const uint64_t new_wal_id = AllocateFileNumber();
    active_wal_id_ = new_wal_id;
    const std::string wal_path = db_path_ + "/" + std::to_string(new_wal_id) + ".wal";
    mem_ = new MemTable(wal_path);
    manifest_state_.live_wals.insert(new_wal_id);
    RecordManifestEdit(ManifestOp::AddWAL, new_wal_id);

    // 恢复WAL
    RecoverFromWals();

    LOG_INFO(std::string("SSTs & WALs Recovery complete. Items in memory: ") + std::to_string(mem_->Count()));
}

DBImpl::~DBImpl() {
    // 析构前最后落盘一次，保证数据不丢
    if (mem_->Count() > 0) {
        MinorCompaction();
    }

    for (auto &level : levels_) {
        for (const auto &reader : level) {
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
    compaction_engine_.MinorCompaction(
        manifest_state_,
        levels_,
        mem_,
        imm_,
        active_wal_id_,
        mutex_,
        [this]() {
            return AllocateFileNumber();
        },
        [this](const ManifestOp op, const uint64_t id, const uint32_t level) {
            RecordManifestEdit(op, id, level);
        });
}

void DBImpl::RecoverFromWals() {
    recovery_loader_.RecoverFromWals(
        manifest_state_,
        mem_,
        [this](const ManifestOp op, const uint64_t id, const uint32_t level) {
            RecordManifestEdit(op, id, level);
        });
}
void DBImpl::LoadSSTables() {
    recovery_loader_.LoadSSTables(
        manifest_state_,
        levels_,
        [this]() {
            return PersistManifestState();
        });
}

uint64_t DBImpl::AllocateFileNumber() {
    ++manifest_state_.next_file_number;
    RecordManifestEdit(ManifestOp::SetNextFileNumber, manifest_state_.next_file_number);
    return manifest_state_.next_file_number;
}

void DBImpl::InitNextFileNumberFromDisk() {
    recovery_loader_.InitNextFileNumberFromDisk(manifest_state_);
}

/*
   参数语义统一为：
       SetNextFileNumber: id = next_file_number，level 忽略
       AddSST: id = file_number，level = level
       DelSST: id = file_number
       AddWAL/DelWAL: id = wal_id
   日志记录格式建议固定为：
       magic(u32) + version(u32) + op(u8) + payload_size(u32)
       payload 按 op 写入（u64 或 u64+u32）
    payload:
        SetNextFileNumber: u64 next_file_number
        AddSST: u64 file_number + u32 level
        DelSST: u64 file_number
        AddWAL/DelWAL: u64 wal_id
*/
bool DBImpl::AppendManifestEdit(ManifestOp op, uint64_t id, uint32_t level) {
    return manifest_manager_.AppendEdit(op, id, level);
}

/*
 * 读取MANIFEST.log
 * IO + 解析 + 顺序驱动
 */
bool DBImpl::ReplayManifestLog() {
    return manifest_manager_.ReplayLog(manifest_state_);
}

/*
 * 纯状态变更
 * 根据 op 改 manifest_state_
 * payload:
        SetNextFileNumber: u64 next_file_number
        AddSST: u64 file_number + u32 level
        DelSST: u64 file_number
        AddWAL/DelWAL: u64 wal_id
 */
bool DBImpl::ApplyManifestEdit(const ManifestOp op, const uint64_t id, const uint32_t level) {
    return ManifestManager::ApplyEdit(manifest_state_, op, id, level);
}

/*
 * 日志主写 + 失败回退
 */
void DBImpl::RecordManifestEdit(ManifestOp op, uint64_t id, uint32_t level) {
    manifest_manager_.RecordEdit(
        manifest_state_,
        manifest_edits_since_checkpoint_,
        op,
        id,
        level);
}

/*
 * 检查更新 MANIFEST.log 到 MANIFEST 快照
 */
void DBImpl::MaybeCheckpointManifest() {
    manifest_manager_.MaybeCheckpoint(
        manifest_state_,
        manifest_edits_since_checkpoint_);
}

/*
 * 暂时作用: 清空MANIFEST.log
 */
bool DBImpl::TruncateManifestLog() {
    return manifest_manager_.TruncateLog();
}

void DBImpl::CompactL0ToL1() {
    compaction_engine_.CompactL0ToL1(
        manifest_state_,
        levels_,
        [this]() {
            return AllocateFileNumber();
        },
        [this](const ManifestOp op, const uint64_t id, const uint32_t level) {
            RecordManifestEdit(op, id, level);
        });
}

size_t DBImpl::LevelSize(const size_t level) const {
    if (level >= levels_.size()) return 0;
    return levels_[level].size();
}

bool DBImpl::LoadManifestState() {
    return manifest_manager_.LoadState(manifest_state_);
}

bool DBImpl::PersistManifestState() {
    return manifest_manager_.PersistState(manifest_state_);
}

std::unique_ptr<DBIterator> DBImpl::NewIterator() {
    std::vector<std::pair<std::string, std::string> > rows;
    std::map<std::string, ValueRecord> seen;
    // 同 key 只保留最新版本，先从mem开始
    // 最新是 kDeletion 就不放入 rows_
    // 最后按 key 升序生成 rows_
    auto it = mem_->GetIterator();
    while (it.Valid()) {
        // 这里的it.value()实际上是ValueRecord
        seen.try_emplace(it.key(), it.value());
        it.Next();
    }

    // 之后是L0，从新到旧，要逆序遍历
    for (auto l = levels_[0].rbegin(); l != levels_[0].rend(); ++l) {
        (*l)->ForEach([&](const std::string &key, const std::string &value, const ValueType type) {
            seen.try_emplace(key, ValueRecord{type, value});
        });
    }

    // L1 同上
    for (auto l = levels_[1].rbegin(); l != levels_[1].rend(); ++l) {
        (*l)->ForEach([&](const std::string &key, const std::string &value, const ValueType type) {
            seen.try_emplace(key, ValueRecord{type, value});
        });
    }

    for (const auto &[fst, snd] : seen) {
        if (snd.type == ValueType::kValue) {
            rows.emplace_back(fst, snd.value);
        }
    }

    return std::make_unique<DBIterator>(std::move(rows));
}

bool DBImpl::Get(const std::string &key, ValueRecord &value) const {
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

void DBImpl::Put(const std::string &key, const ValueRecord &value) {
    // 1. 检查当前 MemTable 是否已满 (假设阈值为 1000 条)
    if (mem_->Count() >= 1000) {
        if (imm_ != nullptr) {
            // 如果上一个 imm_ 还没处理完，为了简单起见，这里先同步等待
            // 后期我们会用后台线程来优化这里
            LOG_WARN("Wait: MinorCompaction is too slow...");
        }
        MinorCompaction();
    }

    // 2. 正常写入
    mem_->Put(key, value);
}
