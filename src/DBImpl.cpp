//
// Created by 26708 on 2026/2/7.
//

#include "DBImpl.h"
#include "FileFormats.h"
#include "Logger.h"
#include "SSTableBuilder.h"
#include <filesystem>

namespace fs = std::filesystem;

DBImpl::DBImpl(std::string db_path)
    : db_path_(db_path), next_file_number_(0), imm_(nullptr) {

    // 1. 确保工作目录存在
    if (!fs::exists(db_path_)) {
        fs::create_directories(db_path_);
    }
    LOG_INFO(std::string("DB path: ") + db_path_);

    // 2. 初始化第一个活跃的 MemTable
    // 每一个 MemTable 对应一个独立的日志文件
    std::string wal_path = db_path_ + "/0.wal";
    mem_ = new MemTable<std::string, std::string>(wal_path);

    // 3. 执行启动恢复 (Recover)
    // 逻辑：如果目录下有旧的 WAL，说明之前有数据没落盘，通过“后门”接口塞进内存
    LOG_INFO("Checking for recovery...");
    mem_->GetWalHandler()->LoadLog([this](OpType type, const std::string& k, const std::string& v) {
        if (type == OpType::ADD) {
            mem_->PutWithoutWal(k, v); // 关键：恢复时不写日志
        } else if (type == OpType::DEL) {
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
    delete mem_;
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

        auto it = imm_->GetIterator(); // 利用你实现的迭代器
        while (it.Valid()) {
            builder.Add(it.key(), it.value());
            it.Next();
        }
        builder.Finish();
        file.Flush();
        LOG_INFO(std::string("SSTable created: ") + sst_path);
    }

    if (SSTableReader* reader = SSTableReader::Open(sst_path)) {
        // 记得加锁，防止 Get 操作同时遍历 readers_
        std::lock_guard<std::mutex> lock(mutex_);
        readers_.push_back(reader);
    } else {
        LOG_ERROR(std::string("Failed to open SSTable: ") + sst_path);
    }

    // 4. 清理：释放 imm_ 的内存
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
    mem_->GetWalHandler()->LoadLog([this](OpType type, const std::string& k, const std::string& v) {
        if (type == OpType::ADD) {
            mem_->PutWithoutWal(k, v); // 恢复时只需写内存，不再写 WAL
        } else {
            mem_->RemoveWithoutWal(k);
        }
    });
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
    for (int i = readers_.size() - 1; i >= 0; --i) {
        if (readers_[i]->Get(key, &value)) {
            LOG_DEBUG(std::string("Get hit: sstable key=") + key);
            return true;
        }
    }

    return false;
}

