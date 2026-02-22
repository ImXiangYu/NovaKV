//
// Created by 26708 on 2026/2/7.
//

#ifndef NOVAKV_DBIMPL_H
#define NOVAKV_DBIMPL_H

#include "MemTable.h"
#include "DBIterator.h"
#include "CompactionEngine.h"
#include "ManifestManager.h"
#include "RecoveryLoader.h"
#include "SSTableReader.h"
#include <string>
#include <vector>
#include <mutex>

class DBImpl {
    public:
        explicit DBImpl(std::string db_path);
        ~DBImpl();

        void Put(const std::string& key, const ValueRecord& value);
        bool Get(const std::string& key, ValueRecord& value) const;
        void CompactL0ToL1();
        size_t LevelSize(size_t level) const;

        // 迭代器
        std::unique_ptr<DBIterator> NewIterator();

    private:
        void MinorCompaction();

        std::string db_path_;
        CompactionEngine compaction_engine_;
        ManifestManager manifest_manager_;
        RecoveryLoader recovery_loader_;

        // 内存层：解耦后的指针
        MemTable *mem_;
        MemTable *imm_;

        // 磁盘层：已打开的 SST 列表
        // levels_[0] 是 L0，levels_[1] 是 L1
        std::vector<std::vector<SSTableReader*>> levels_;

        // 维护active_wal_id_
        uint64_t active_wal_id_ = 0;

        // 保护元数据和 mem/imm 切换的锁
        std::mutex mutex_;
};

#endif //NOVAKV_DBIMPL_H
