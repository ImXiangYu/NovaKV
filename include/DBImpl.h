//
// Created by 26708 on 2026/2/7.
//

#ifndef NOVAKV_DBIMPL_H
#define NOVAKV_DBIMPL_H

#include <string>
#include "MemTable.h"

#include <vector>
#include <mutex>
#include "SSTableReader.h"

class DBImpl {
    public:
        explicit DBImpl(std::string db_path);
        ~DBImpl();

        void Put(const std::string& key, const std::string& value);
        bool Get(const std::string& key, std::string& value);

    private:
        void MinorCompaction();
        void Recover(); // 关键：启动时自动恢复逻辑

        std::string db_path_;
        int next_file_number_;

        // 内存层：解耦后的指针
        MemTable<std::string, std::string>* mem_;
        MemTable<std::string, std::string>* imm_;

        // 磁盘层：已打开的 SST 列表
        std::vector<SSTableReader*> readers_;

        // 保护元数据和 mem/imm 切换的锁
        std::mutex mutex_;
};

#endif //NOVAKV_DBIMPL_H