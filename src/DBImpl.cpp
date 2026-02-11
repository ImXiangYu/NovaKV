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

namespace fs = std::filesystem;

DBImpl::DBImpl(std::string db_path)
    : db_path_(db_path), next_file_number_(0), imm_(nullptr) {

    // 1. 确保工作目录存在
    if (!fs::exists(db_path_)) {
        fs::create_directories(db_path_);
    }
    LOG_INFO(std::string("DB path: ") + db_path_);

    // 2. 初始化levels_
    // 先初始化为两层，即levels_[0] 是 L0，levels_[1] 是 L1
    levels_.resize(2);

    // 调用LoadSSTables()恢复数据
    LoadSSTables();

    // 3. 初始化第一个活跃的 MemTable
    // 每一个 MemTable 对应一个独立的日志文件
    std::string wal_path = db_path_ + "/0.wal";
    mem_ = new MemTable<std::string, std::string>(wal_path);

    // 4. 执行启动恢复 (Recover)
    // 逻辑：如果目录下有旧的 WAL，说明之前有数据没落盘，通过“后门”接口塞进内存
    LOG_INFO("Checking for recovery...");
    mem_->GetWalHandler()->LoadLog([this](ValueType type, const std::string& k, const std::string& v) {
        if (type == ValueType::kValue) {
            mem_->PutWithoutWal(k, v); // 关键：恢复时不写日志
        } else if (type == ValueType::kDeletion) {
            mem_->RemoveWithoutWal(k);
        }
    });
    LOG_INFO(std::string("Recovery complete. Items in memory: ") + std::to_string(mem_->Count()));
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
    imm_ = nullptr;

    // 清理mem_
    delete mem_;
    mem_ = nullptr;
}

void DBImpl::Put(const std::string &key, const std::string &value) {
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

void DBImpl::MinorCompaction() {
    LOG_INFO("Minor Compaction triggered...");
    std::string old_wal_path = mem_->GetWalPath();

    // 1. 冻结 mem_ 到 imm_
    imm_ = mem_;
    LOG_INFO(std::string("Immutable MemTable items: ") + std::to_string(imm_->Count()));

    // 2. 开启全新的 mem_ 和新的 WAL 文件
    std::string new_wal = db_path_ + "/" + std::to_string(++next_file_number_) + ".wal";
    mem_ = new MemTable<std::string, std::string>(new_wal);

    // 3. 将 imm_ 落盘为 SSTable
    std::string sst_path = db_path_ + "/" + std::to_string(next_file_number_) + ".sst";

    // 封装落盘逻辑
    {
        WritableFile file(sst_path);
        SSTableBuilder builder(&file);

        auto it = imm_->GetIterator();
        while (it.Valid()) {
            builder.Add(it.key(), it.value(), ValueType::kValue);
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
        // 删除旧 WAL 文件
        if (std::filesystem::exists(old_wal_path)) {
            std::filesystem::remove(old_wal_path);
            LOG_INFO(std::string("Removed old wal file: ") + old_wal_path);
        } else {
            LOG_WARN(std::string("WAL path does not exist: ") + old_wal_path);
        }
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

void DBImpl::Recover() {
    // 1. 实际开发中这里会扫描目录下的 .wal 文件
    // 我们先实现最核心的：重放 active.wal
    std::string wal_path = db_path_ + "/active.wal";

    // 这里利用你写好的 LoadLog 回调逻辑
    // WalHandler 会自动校验 CRC，数据不全或损坏会自动 break
    mem_->GetWalHandler()->LoadLog([this](ValueType type, const std::string& k, const std::string& v) {
        if (type == ValueType::kValue) {
            mem_->PutWithoutWal(k, v); // 恢复时只需写内存，不再写 WAL
        } else {
            mem_->RemoveWithoutWal(k);
        }
    });
}
void DBImpl::LoadSSTables() {
    LOG_INFO(std::string("LoadSSTables start"));
    std::vector<std::pair<int, std::string>> sstables;
    int max_id = 0;
    for (auto& entry : std::filesystem::directory_iterator(db_path_)) {
        // 如果每一条的扩展名是.sst，就记录下来
        if (entry.is_regular_file()) {
            if (entry.path().extension() == ".sst") {
                std::string stem = entry.path().stem().string();
                // 如果不是全数字就跳过
                if (!std::all_of(stem.begin(), stem.end(),
                                 [](const unsigned char c) { return std::isdigit(c); })) {
                    continue;
                }
                int id = std::stoi(stem);
                sstables.emplace_back(id, entry.path().string());
                max_id = std::max(max_id, id);
            }
        }
    }
    // 之后把sstables从小到大排序，排序后重新Open加到levels_[0]即可
    std::sort(sstables.begin(), sstables.end());
    next_file_number_ = std::max(next_file_number_, max_id);
    for (const auto & sstable : sstables) {
        if (SSTableReader* reader = SSTableReader::Open(sstable.second)) {
            LOG_INFO(std::string("SSTable recovery: ") + sstable.second);
            // 加回levels_[0]
            levels_[0].push_back(reader);
        }
    }
    LOG_INFO(std::string("LoadSSTables completed"));
}
void DBImpl::CompactL0ToL1() {
    // 先判空，无需合并
    if (levels_[0].empty()) return;
    std::map<std::string, std::string> mp;
    // 倒序遍历levels_[0]
    for (auto it = levels_[0].rbegin(); it != levels_[0].rend(); ++it) {
        (*it)->ForEach([&](const std::string &key, const std::string &value) {
            if (!mp.count(key)) {mp[key] = value;}
        });
    }

    // 合并前先判空
    if (mp.empty()) {
        for (auto reader : levels_[0]) {
            delete reader;
        }
        levels_[0].clear();
        return;
    }

    std::string new_sst_path = db_path_ + "/" + std::to_string(++next_file_number_) + ".sst";

    WritableFile file(new_sst_path);
    SSTableBuilder builder(&file);

    for (const auto& pair : mp) {
        builder.Add(pair.first, pair.second, ValueType::kValue);
    }
    builder.Finish();
    file.Flush();

    if (SSTableReader* reader = SSTableReader::Open(new_sst_path)) {
        LOG_INFO(std::string("SSTable created: ") + new_sst_path);
        // 暂不做 L1 合并，后续会引入更低层压实
        levels_[1].push_back(reader);
    }

    for (auto reader : levels_[0]) {
        delete reader;
    }
    levels_[0].clear();
}

size_t DBImpl::LevelSize(const size_t level) const {
    if (level >= levels_.size()) return 0;
    return levels_[level].size();
}

bool DBImpl::Get(const std::string& key, std::string& value) {
    // 第一级：查找活跃内存 (MemTable)
    if (mem_ && mem_->Get(key, value)) {
        LOG_DEBUG(std::string("Get hit: memtable key=") + key);
        return true;
    }

    // 第二级：查找只读内存 (Immutable MemTable)
    // 注意：如果 MinorCompaction 正在进行，imm_ 里的数据也是最新的
    if (imm_ && imm_->Get(key, value)) {
        LOG_DEBUG(std::string("Get hit: immutable memtable key=") + key);
        return true;
    }

    // 第三级：查找磁盘 SSTable (从新到旧)
    // 越晚生成的 SST 文件，数据越新，所以要逆序遍历
    // 先倒序遍历 levels_[0]（L0 新到旧）
    for (size_t i = levels_[0].size(); i-- > 0;) {
        if (levels_[0][i]->Get(key, &value)) {
            LOG_DEBUG(std::string("Get hit in L0: sstable key=") + key);
            return true;
        }
    }

    // 再倒序遍历 levels_[1]（L1 新到旧）
    for (size_t i = levels_[1].size(); i-- > 0;) {
        if (levels_[1][i]->Get(key, &value)) {
            LOG_DEBUG(std::string("Get hit in L1: sstable key=") + key);
            return true;
        }
    }

    return false;
}

