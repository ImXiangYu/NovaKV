//
// Created by 26708 on 2026/2/4.
//

#ifndef NOVAKV_MEMTABLE_H
#define NOVAKV_MEMTABLE_H

#include <mutex>
#include <shared_mutex>
#include "Logger.h"
#include "SkipList.h"
#include "ValueRecord.h"
#include "WalHandler.h"

class MemTable {
    private:
        // 顺序很重要：先 WAL，再 Table
        // Member Initializer List Order Awareness
        // 初始化时：先初始化 wal_，再初始化 table_。这样 RecoverFromWal 执行时，文件句柄已经完全准备好了。
        // 析构时：先销毁 table_，再销毁 wal_。这保证了在内存索引销毁的过程中，如果还有任何最后的日志要写，wal_ 依然是有效的。
        WalHandler wal_;
        SkipList<std::string, ValueRecord> table_;
        mutable  std::shared_mutex rw_lock_; // 读写锁

    public:
        // MemTable(int max_level = 16, const std::string& wal_file) : table_(max_level), wal_(wal_file) {}
        // 当构造函数中既有带默认值的参数，又有必须传递的参数时，C++ 规定：默认实参必须从右向左排列。
        MemTable(const std::string& wal_file, int max_level = 16) : wal_(wal_file), table_(max_level) {}

        // 插入或更新
        void Put(const std::string& key, const ValueRecord& value) {
            // 加个写锁，确保同一时间只有一个线程在修改SkipList
            std::unique_lock lock(rw_lock_);
            // 关键：先写日志，再改内存！
            wal_.AddLog(key, value.value, value.type);
            // 加锁后直接调用insert方式
            table_.insert_element(key, value);
        }
        // 查询
        bool Get(const std::string& key, ValueRecord& value) {
            // 加个读锁，确保同一时间多个线程可以同时读
            std::shared_lock lock(rw_lock_);
            return table_.search_element(key, value);
        }
        // 删除
        bool Remove(const std::string& key) {
            // 同样要加写锁
            std::unique_lock lock(rw_lock_);
            // 关键：先写日志，再改内存！
            // Put 写 kValue, Remove 写 kDeletion
            wal_.AddLog(key, "", ValueType::kDeletion);
            // remove 不再物理删除节点，而是写入tombstone
            table_.insert_element(key, {ValueType::kDeletion, ""});
            return true;
        }

        // 获取当前数量
        int Count() const {
            std::shared_lock lock(rw_lock_);
            return table_.size();
        }

        auto GetIterator() {
            // 记得加读锁，虽然此时 imm_ 是只读的，但养成习惯没坏处
            return table_.begin();
        }

        // [DBImpl]
        // 1. 获取 WalHandler 指针，方便 DBImpl 调用 LoadLog
        WalHandler* GetWalHandler() { return &wal_; }

        void ApplyWithoutWal(const std::string& key, const ValueRecord& value) {
            std::unique_lock lock(rw_lock_);
            table_.insert_element(key, value);
        }

        // 4. 获取内存占用估算 (字节)
        // 这是一个硬核指标，用于触发 Minor Compaction
        size_t ApproximateMemoryUsage() const {
            // 粗略计算：SkipList 节点数 * (Key平均大小 + Value平均大小 + 指针开销)
            // 简单起见，可以先返回 table_.size() * 100 (假设平均每条 100 字节)
            // 或者在 SkipList 里维护一个精确的 byte_counter
            return table_.size() * 128;
        }

        // 获取Path
        std::string GetWalPath() const {
            return wal_.GetFilename();
        }
};

#endif //NOVAKV_MEMTABLE_H
