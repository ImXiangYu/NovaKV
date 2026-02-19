//
// Created by 26708 on 2026/2/7.
//

#ifndef NOVAKV_DBIMPL_H
#define NOVAKV_DBIMPL_H

#include <string>
#include "MemTable.h"

#include <vector>
#include <mutex>

#include "DBIterator.h"
#include "SSTableReader.h"

class DBImpl {
    public:
        explicit DBImpl(std::string db_path);
        ~DBImpl();

        void Put(const std::string& key, const ValueRecord& value);
        bool Get(const std::string& key, ValueRecord& value) const;
        void CompactL0ToL1();
        size_t LevelSize(size_t level) const;

        // 记录与恢复next_file_number_
        bool LoadNextFileNumberFromManifest();
        void PersistNextFileNumber() const;

        // 迭代器
        std::unique_ptr<DBIterator> NewIterator();

    private:
        void MinorCompaction();
        void RecoverFromWals() const; // 关键：启动时自动恢复逻辑
        void LoadSSTables();

        // 管理next_file_number_
        int AllocateFileNumber();
        // 负责计算并设置next_file_number_
        void InitNextFileNumberFromDisk();

        // 辅助检查
        bool HasVisibleValueInL1(const std::string& key) const;

        std::string db_path_;
        int next_file_number_;

        // 内存层：解耦后的指针
        MemTable *mem_;
        MemTable *imm_;

        // 磁盘层：已打开的 SST 列表
        // levels_[0] 是 L0，levels_[1] 是 L1
        std::vector<std::vector<SSTableReader*>> levels_;

        // 保护元数据和 mem/imm 切换的锁
        std::mutex mutex_;
};

#endif //NOVAKV_DBIMPL_H
