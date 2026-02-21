//
// Created by 26708 on 2026/2/7.
//

#ifndef NOVAKV_DBIMPL_H
#define NOVAKV_DBIMPL_H

#include "MemTable.h"
#include "DBIterator.h"
#include "SSTableReader.h"
#include <string>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <unordered_map>

enum class ManifestOp : uint8_t {
    SetNextFileNumber=1, AddSST=2, DelSST=3, AddWAL=4, DelWAL=5
};

class DBImpl {
    public:
        explicit DBImpl(std::string db_path);
        ~DBImpl();

        void Put(const std::string& key, const ValueRecord& value);
        bool Get(const std::string& key, ValueRecord& value) const;
        void CompactL0ToL1();
        size_t LevelSize(size_t level) const;

        // 使用Manifest记录sst状态
        bool LoadManifestState();
        void PersistManifestState();

        // 迭代器
        std::unique_ptr<DBIterator> NewIterator();

    private:
        void MinorCompaction();
        void RecoverFromWals(); // 关键：启动时自动恢复逻辑
        void LoadSSTables();

        // 管理next_file_number_
        uint64_t AllocateFileNumber();
        // 负责计算并设置next_file_number_
        void InitNextFileNumberFromDisk();

        // 辅助检查
        bool HasVisibleValueInL1(const std::string& key) const;

        // Manifest 日志
        bool AppendManifestEdit(ManifestOp op, uint64_t id, uint32_t level = 0) const;

        struct ManifestState {
            uint64_t next_file_number = 0;
            std::unordered_map<uint64_t, uint32_t> sst_levels; // file_number -> level
            std::unordered_set<uint64_t> live_wals;            // 为多 WAL 恢复闭环预留
        };
        ManifestState manifest_state_;

        std::string db_path_;

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
