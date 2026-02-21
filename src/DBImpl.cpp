//
// Created by 26708 on 2026/2/7.
//

#include "DBImpl.h"
#include "FileFormats.h"
#include "Logger.h"
#include "SSTableBuilder.h"
#include <filesystem>
#include <map>
#include <algorithm>
#include <cctype>
#include <utility>

namespace fs = std::filesystem;

namespace {
constexpr uint32_t kManifestMagic = 0x12345678; // 自定义
constexpr uint32_t kManifestVersion = 1;
} // 匿名

DBImpl::DBImpl(std::string db_path)
    : db_path_(std::move(db_path)), imm_(nullptr) {

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

    // 调用LoadSSTables()恢复数据
    LoadSSTables();

    // 3. 初始化第一个活跃的 MemTable
    // 每一个 MemTable 对应一个独立的日志文件
    const uint64_t new_wal_id = AllocateFileNumber();
    active_wal_id_ = new_wal_id;
    const std::string wal_path = db_path_ + "/" + std::to_string(new_wal_id) + ".wal";
    mem_ = new MemTable(wal_path);
    manifest_state_.live_wals.insert(new_wal_id);
    PersistManifestState();

    // 恢复WAL
    RecoverFromWals();

    LOG_INFO(std::string("SSTs & WALs Recovery complete. Items in memory: ") + std::to_string(mem_->Count()));
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
    LOG_INFO("Minor Compaction triggered...");
    // 先保存旧wal_id
    const uint64_t old_wal_id = active_wal_id_;
    std::string old_wal_path = mem_->GetWalPath();

    // 1. 冻结 mem_ 到 imm_
    imm_ = mem_;
    LOG_INFO(std::string("Immutable MemTable items: ") + std::to_string(imm_->Count()));

    // 2. 开启全新的 mem_ 和新的 WAL 文件
    uint64_t new_wal_id = AllocateFileNumber();
    active_wal_id_ = new_wal_id;
    std::string new_wal = db_path_ + "/" + std::to_string(new_wal_id) + ".wal";
    mem_ = new MemTable(new_wal);

    manifest_state_.live_wals.insert(new_wal_id);
    PersistManifestState();

    // 3. 将 imm_ 落盘为 SSTable
    uint64_t new_sst_id = AllocateFileNumber();
    std::string sst_path = db_path_ + "/" + std::to_string(new_sst_id) + ".sst";

    // 封装落盘逻辑
    {
        WritableFile file(sst_path);
        SSTableBuilder builder(&file);

        auto it = imm_->GetIterator();
        while (it.Valid()) {
            builder.Add(it.key(), it.value().value, it.value().type);
            it.Next();
        }
        builder.Finish();
        file.Flush();
        LOG_INFO(std::string("SSTable created: ") + sst_path);
    }

    if (SSTableReader* level = SSTableReader::Open(sst_path)) {
        // 记得加锁，防止 Get 操作同时遍历 levels[0]
        {
            std::lock_guard<std::mutex> lock(mutex_);
            levels_[0].push_back(level);
        }

        manifest_state_.sst_levels[new_sst_id] = 0;
        // 删除旧 WAL 文件
        if (fs::exists(old_wal_path) && fs::remove(old_wal_path)) {
            manifest_state_.live_wals.erase(old_wal_id);
            LOG_INFO(std::string("Removed old wal file: ") + old_wal_path);
        } else {
            LOG_WARN(std::string("WAL path does not exist: ") + old_wal_path);
        }

        PersistManifestState();
    } else {
        LOG_ERROR(std::string("Failed to open SSTable: ") + sst_path);
    }

    // 4. 触发Compact
    if (levels_[0].size() >= 2) {
        CompactL0ToL1();
    }

    // 5. 清理：释放 imm_ 的内存
    // 在工业级实现里，这里还要删除对应的旧 WAL 文件，我们先只释放指针
    delete imm_;
    imm_ = nullptr;
}

void DBImpl::RecoverFromWals() {
    LOG_INFO(std::string("Recover from wals start."));

    // 兜底扫描目录，把旧目录中的 WAL 补录进 manifest_state_
    bool manifest_changed = false;
    for (auto& entry : fs::directory_iterator(db_path_)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".wal") {
            continue;
        }
        const std::string stem = entry.path().stem().string();
        if (!std::all_of(stem.begin(), stem.end(),
                         [](const unsigned char c) { return std::isdigit(c); })) {
            continue;
        }
        if (const uint64_t id = std::stoull(stem); manifest_state_.live_wals.insert(id).second) {
            manifest_changed = true;
        }
    }
    if (manifest_changed) {
        PersistManifestState();
    }

    std::vector replay_ids(
        manifest_state_.live_wals.begin(), manifest_state_.live_wals.end());
    std::sort(replay_ids.begin(), replay_ids.end());

    for (const uint64_t id : replay_ids) {
        const std::string wal_path = db_path_ + "/" + std::to_string(id) + ".wal";
        if (!fs::exists(wal_path)) {
            LOG_WARN(std::string("Manifest WAL missing on disk: ") + wal_path);
            continue;
        }
        WalHandler handler(wal_path);
        handler.LoadLog([this](ValueType type, const std::string& k, const std::string& v) {
            mem_->ApplyWithoutWal(k, {type, v});
        });
    }
    LOG_INFO(std::string("Recover from wals completed."));
}
void DBImpl::LoadSSTables() {
    LOG_INFO(std::string("LoadSSTables start"));

    // 通过manifest获取
    // 先清空levels_
    for (auto& lv : levels_) {
        for (const auto* r : lv) delete r;
        lv.clear();
    }

    if (!manifest_state_.sst_levels.empty()) {
        std::vector<std::pair<uint64_t, uint32_t>> entries(
    manifest_state_.sst_levels.begin(), manifest_state_.sst_levels.end());
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        for (const auto& [id, level] : entries) {
            if (level >= levels_.size()) {
                LOG_ERROR("Manifest level out of range: " + std::to_string(level));
                continue;
            }

            const std::string path = db_path_ + "/" + std::to_string(id) + ".sst";
            if (!fs::exists(path)) {
                LOG_ERROR("Manifest SST missing: " + path);
                continue;
            }

            if (SSTableReader* reader = SSTableReader::Open(path)) {
                levels_[level].push_back(reader);
            } else {
                LOG_ERROR("Failed to open manifest SST: " + path);
            }
        }
        // 成功后直接return
        LOG_INFO(std::string("LoadSSTables completed"));
        return ;
    }

    // 保留扫目录分支
    std::vector<std::pair<int, std::string>> sstables;
    for (auto& entry : std::filesystem::directory_iterator(db_path_)) {
        // 如果每一条的扩展名是.sst，就记录下来
        if (!entry.is_regular_file() || entry.path().extension() != ".sst") continue;
        const std::string stem = entry.path().stem().string();
        if (!std::all_of(stem.begin(), stem.end(),
                         [](unsigned char c){ return std::isdigit(c); })) {
            continue;
                         }
        const uint64_t id = std::stoull(stem);
        sstables.emplace_back(id, entry.path().string());
    }
    // 之后把sstables从小到大排序，排序后重新Open加到levels_[0]即可
    std::sort(sstables.begin(), sstables.end());

    for (const auto& [id, path] : sstables) {
        if (SSTableReader* reader = SSTableReader::Open(path)) {
            levels_[0].push_back(reader);              // 旧逻辑兜底：先归到 L0
            manifest_state_.sst_levels[id] = 0;        // 迁移写回Manifest
        }
    }

    if (!manifest_state_.sst_levels.empty()) {
        PersistManifestState(); // 完成一次迁移落盘
    }

    LOG_INFO(std::string("LoadSSTables completed"));
}

uint64_t DBImpl::AllocateFileNumber() {
    // 分配文件编号，避免到处++next_file_number_ 导致漏改。
    // 这里++之后要立即PersistNextFileNumber
    ++manifest_state_.next_file_number;
    // PersistNextFileNumber();
    PersistManifestState();
    return manifest_state_.next_file_number;
}

void DBImpl::InitNextFileNumberFromDisk() {
    // 负责计算并设置next_file_number_
    int max_id = 0;
    for (auto& entry : std::filesystem::directory_iterator(db_path_)) {
        // 如果每一条的扩展名是.sst 或 .wal ，就记录下来
        if (entry.is_regular_file()) {
            if (entry.path().extension() == ".sst" || entry.path().extension() == ".wal") {
                std::string stem = entry.path().stem().string();
                // 如果不是全数字就跳过
                if (!std::all_of(stem.begin(), stem.end(),
                                 [](const unsigned char c) { return std::isdigit(c); })) {
                    continue;
                                 }
                int id = std::stoi(stem);
                max_id = std::max(max_id, id);
            }
        }
    }
    manifest_state_.next_file_number = max_id;
    // next_file_number_ = max_id;
}
bool DBImpl::HasVisibleValueInL1(const std::string &key) const {
    for (size_t i = levels_[1].size(); i-- > 0;) {
        ValueRecord rec{ValueType::kDeletion, ""};
        if (levels_[1][i]->GetRecord(key, &rec)) {
            if (rec.type == ValueType::kDeletion) {
                return false;
            }
            return true;
        }
    }
    // 没找到
    return false;
}

/*
   参数语义统一为：
       SetNextFileNumber: id = next_file_number，level 忽略
       AddSST: id = file_number，level = level
       DelSST: id = file_number
       AddWAL/DelWAL: id = wal_id
   日志记录格式建议固定为：
       magic(u32) + version(u16) + op(u8) + payload_size(u32)
       payload 按 op 写入（u64 或 u64+u32）
    payload:
        SetNextFileNumber: u64 next_file_number
        AddSST: u64 file_number + u32 level
        DelSST: u64 file_number
        AddWAL/DelWAL: u64 wal_id
*/
bool DBImpl::AppendManifestEdit(ManifestOp op, uint64_t id, uint32_t level) {
    if (!fs::exists(db_path_) || !fs::is_directory(db_path_)) {
        LOG_ERROR("DB Path error: " + db_path_);
        return false;
    }

    // MANIFEST.log
    const fs::path final_path = fs::path(db_path_) / "MANIFEST.log";
    const fs::path tmp_path = fs::path(db_path_) / "MANIFEST.log.tmp";

    // 先写入临时文件
    if (std::ofstream ofs(tmp_path, std::ios::binary | std::ios::trunc); ofs) {
        uint32_t payload_size = 0;
        switch (op) {
            case ManifestOp::SetNextFileNumber: payload_size = sizeof(uint64_t); break;
            case ManifestOp::AddSST:            payload_size = sizeof(uint64_t) + sizeof(uint32_t); break;
            case ManifestOp::DelSST:            payload_size = sizeof(uint64_t); break;
            case ManifestOp::AddWAL:            payload_size = sizeof(uint64_t); break;
            case ManifestOp::DelWAL:            payload_size = sizeof(uint64_t); break;
            default: return false;
        }

        // 写 header（字段分开写，不要直接写 struct，避免 padding）
        constexpr uint32_t magic = kManifestMagic;
        constexpr uint32_t version = kManifestVersion;
        const auto op_u8 = static_cast<uint8_t>(op);

        ofs.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        ofs.write(reinterpret_cast<const char*>(&version), sizeof(version));
        ofs.write(reinterpret_cast<const char*>(&op_u8), sizeof(op_u8));
        ofs.write(reinterpret_cast<const char*>(&payload_size), sizeof(payload_size));

        // 写 payload
        switch (op) {
            case ManifestOp::SetNextFileNumber:
            case ManifestOp::DelSST:
            case ManifestOp::AddWAL:
            case ManifestOp::DelWAL:
                ofs.write(reinterpret_cast<const char*>(&id), sizeof(id));
                break;
            case ManifestOp::AddSST:
                ofs.write(reinterpret_cast<const char*>(&id), sizeof(id));       // file_number
                ofs.write(reinterpret_cast<const char*>(&level), sizeof(level)); // level
                break;
            default:
                return false;
        }

        ofs.flush(); // 确保操作系统缓冲区已接收数据
        if (!ofs) {
            LOG_ERROR("Failed to write MANIFEST.log content");
            return false;
        }
        // 显式关闭流，确保文件句柄释放，否则在某些系统上 rename 会失败
        ofs.close();
    } else {
        LOG_ERROR("Failed to open MANIFEST.log.tmp for writing");
        return false;
    }

    // 原子替换：将 tmp 重命名为正式文件
    fs::rename(tmp_path, final_path);
    return true;
}
void DBImpl::CompactL0ToL1() {
    // 先判空，无需合并
    if (levels_[0].empty()) return;
    std::map<std::string, ValueRecord> mp;
    // 倒序遍历levels_[0]
    for (auto it = levels_[0].rbegin(); it != levels_[0].rend(); ++it) {
        (*it)->ForEach([&](const std::string &key, const std::string &value, const ValueType type) {
            mp.try_emplace(key, ValueRecord{type, value});
        });
    }

    // 先记录本轮输入（当前你是“吃掉全部 L0”，可先取 manifest 里 level=0 的 id）
    std::vector<uint64_t> l0_input_ids;
    for (const auto& [id, level] : manifest_state_.sst_levels) {
        if (level == 0) l0_input_ids.push_back(id);
    }

    auto consume_l0 = [&]() {
        for (const auto* r : levels_[0]) {
            delete r;
        }
        levels_[0].clear();

        for (uint64_t id : l0_input_ids) {
            manifest_state_.sst_levels.erase(id);
            fs::remove(fs::path(db_path_) / (std::to_string(id) + ".sst"));
        }
    };

    // mp 为空：无输出，但输入已被消费
    if (mp.empty()) {
        consume_l0();
        PersistManifestState();
        return;
    }

    const uint64_t new_sst_id = AllocateFileNumber();
    std::string new_sst_path = db_path_ + "/" + std::to_string(new_sst_id) + ".sst";

    WritableFile file(new_sst_path);
    SSTableBuilder builder(&file);

    // 确认需要输出sst
    bool has_output = false;
    for (const auto&[key, record] : mp) {
        if (record.type == ValueType::kValue) {
            builder.Add(key, record.value, ValueType::kValue);
            has_output = true;
        } else {
            // 如果是kDeletion，就按条件写
            if (HasVisibleValueInL1(key)) {
                // 需要遮蔽旧值
                builder.Add(key, "", ValueType::kDeletion);
                has_output = true;
                // 不需要就不写，意味着删除
            }
        }
    }
    builder.Finish();
    file.Flush();

    if (!has_output) {
        // 没输出就删除
        fs::remove(new_sst_path);
        consume_l0();
        PersistManifestState();
        return;
    }

    if (SSTableReader* reader = SSTableReader::Open(new_sst_path)) {
        LOG_INFO(std::string("SSTable created: ") + new_sst_path);
        // 暂不做 L1 合并，后续会引入更低层压实
        levels_[1].push_back(reader);
        manifest_state_.sst_levels[new_sst_id] = 1;
        consume_l0();
        PersistManifestState();
        return;
    }

    // 打开新 SST 失败：不消费旧 L0，保持当前进程可见数据
    LOG_ERROR(std::string("Failed to open SSTable: ") + new_sst_path);
    fs::remove(new_sst_path);
}

size_t DBImpl::LevelSize(const size_t level) const {
    if (level >= levels_.size()) return 0;
    return levels_[level].size();
}

bool DBImpl::LoadManifestState() {
    // 将写入元数据文件中的State读出来
    // 在db_path_中找一个MANIFEST文件
    LOG_INFO("Load manifest state start.");
    if (!fs::exists(db_path_) || !fs::is_directory(db_path_)) {
        LOG_ERROR("DB Path error: " + db_path_);
        return false;
    }

    const fs::path p = fs::path(db_path_) / "MANIFEST";

    if (!fs::exists(p)) {
        return false;
    }

    if (std::ifstream ifs(p, std::ios::binary); ifs) {
        // 文件布局:
        // magic, version, next_file_number, sst_count,
        // [file_number, level] * sst_count, wal_count, [wal_id] * wal_count
        uint32_t magic = 0;
        uint32_t version = 0;
        uint64_t next_file_number = 0;
        uint32_t sst_count = 0;
        uint32_t wal_count = 0;

        if (!ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic))) return false;
        if (magic != kManifestMagic) return false;
        if (!ifs.read(reinterpret_cast<char*>(&version), sizeof(version))) return false;
        if (version != kManifestVersion) return false;
        if (!ifs.read(reinterpret_cast<char*>(&next_file_number), sizeof(next_file_number))) return false;
        if (!ifs.read(reinterpret_cast<char*>(&sst_count), sizeof(sst_count))) return false;

        manifest_state_.next_file_number = next_file_number;
        manifest_state_.sst_levels.clear();
        for (uint32_t i = 0; i < sst_count; ++i) {
            uint64_t file_number = 0;
            uint32_t level = 0;
            if (!ifs.read(reinterpret_cast<char*>(&file_number), sizeof(file_number))) return false;
            if (!ifs.read(reinterpret_cast<char*>(&level), sizeof(level))) return false;
            manifest_state_.sst_levels[file_number] = level;
        }

        manifest_state_.live_wals.clear();
        if (!ifs.read(reinterpret_cast<char*>(&wal_count), sizeof(wal_count))) return false;
        for (uint32_t i = 0; i < wal_count; ++i) {
            uint64_t wal_id = 0;
            if (!ifs.read(reinterpret_cast<char*>(&wal_id), sizeof(wal_id))) return false;
            manifest_state_.live_wals.insert(wal_id);
        }
    } else {
        LOG_INFO("Manifest file can't be opened.");
        return false;
    }
    LOG_INFO("Load manifest state completed.");
    return true;
}

void DBImpl::PersistManifestState() {
    // 将State写入元数据文件
    // 在db_path_中找一个MANIFEST文件
    if (!fs::exists(db_path_) || !fs::is_directory(db_path_)) {
        LOG_ERROR("DB Path error: " + db_path_);
        return;
    }

    const fs::path final_path = fs::path(db_path_) / "MANIFEST";
    const fs::path tmp_path = fs::path(db_path_) / "MANIFEST.tmp";

    // 先写入临时文件
    if (std::ofstream ofs(tmp_path, std::ios::binary | std::ios::trunc); ofs) {
        ofs.write(reinterpret_cast<const char*>(&kManifestMagic), sizeof(kManifestMagic));
        ofs.write(reinterpret_cast<const char*>(&kManifestVersion), sizeof(kManifestVersion));
        ofs.write(reinterpret_cast<const char*>(&manifest_state_.next_file_number),
                  sizeof(manifest_state_.next_file_number));

        // SST 映射数量
        const auto sst_count = static_cast<uint32_t>(manifest_state_.sst_levels.size());
        ofs.write(reinterpret_cast<const char*>(&sst_count), sizeof(sst_count));

        // 为了文件稳定可读，按 file_number 排序后写入
        std::vector<std::pair<uint64_t, uint32_t>> sst_entries(
            manifest_state_.sst_levels.begin(), manifest_state_.sst_levels.end());
        std::sort(sst_entries.begin(), sst_entries.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        for (const auto& [file_number, level] : sst_entries) {
            ofs.write(reinterpret_cast<const char*>(&file_number), sizeof(file_number));
            ofs.write(reinterpret_cast<const char*>(&level), sizeof(level));
        }

        // WAL 数量（你现在可先写 0 或写 live_wals 真实值）
        const auto wal_count = static_cast<uint32_t>(manifest_state_.live_wals.size());
        ofs.write(reinterpret_cast<const char*>(&wal_count), sizeof(wal_count));
        for (const uint64_t wal_id : manifest_state_.live_wals) {
            ofs.write(reinterpret_cast<const char*>(&wal_id), sizeof(wal_id));
        }

        ofs.flush(); // 确保操作系统缓冲区已接收数据
        if (!ofs) {
            LOG_ERROR("Failed to write MANIFEST content");
            return;
        }

        // 显式关闭流，确保文件句柄释放，否则在某些系统上 rename 会失败
        ofs.close();
    } else {
        LOG_ERROR("Failed to open MANIFEST.tmp for writing");
        return;
    }

    // 原子替换：将 tmp 重命名为正式文件
    fs::rename(tmp_path, final_path);
}

std::unique_ptr<DBIterator> DBImpl::NewIterator() {
    std::vector<std::pair<std::string, std::string>> rows;
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

bool DBImpl::Get(const std::string& key, ValueRecord& value) const {
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

