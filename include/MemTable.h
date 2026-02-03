//
// Created by 26708 on 2026/2/4.
//

#ifndef NOVAKV_MEMTABLE_H
#define NOVAKV_MEMTABLE_H

#include <mutex>
#include <shared_mutex>
#include "SkipList.h"

template<typename K, typename V>
class MemTable {
    private:
        SkipList<K, V> table_;
        mutable  std::shared_mutex rw_lock_; // 读写锁

    public:
        MemTable(int max_level = 16) : table_(max_level) {}

        // 插入或更新
        void Put(const K& key, const V& value) {
            // 加个写锁，确保同一时间只有一个线程在修改SkipList
            std::unique_lock<std::shared_mutex> lock(rw_lock_);
            // 加锁后直接调用insert方式
            table_.insert_element(key, value);
        }
        // 查询
        bool Get(const K& key, V& value) {
            // 加个读锁，确保同一时间多个线程可以同时读
            std::shared_lock<std::shared_mutex> lock(rw_lock_);
            return table_.search_element(key, value);
        }
        // 删除
        bool Remove(const K& key) {
            // 同样要加写锁
            std::unique_lock<std::shared_mutex> lock(rw_lock_);
            // 加锁后直接调用delete
            return table_.delete_element(key);
        }

        // 获取当前数量
        int Count() const {
            std::shared_lock<std::shared_mutex> lock(rw_lock_);
            return table_.size();
        }
};

#endif //NOVAKV_MEMTABLE_H