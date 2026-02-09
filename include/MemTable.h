//
// Created by 26708 on 2026/2/4.
//

#ifndef NOVAKV_MEMTABLE_H
#define NOVAKV_MEMTABLE_H

#include <cstring>
#include <mutex>
#include <shared_mutex>
#include "Logger.h"
#include "SkipList.h"
#include "WalHandler.h"

template<typename K, typename V>
class MemTable {
    private:
        // 顺序很重要：先 WAL，再 Table
        // Member Initializer List Order Awareness
        // 初始化时：先初始化 wal_，再初始化 table_。这样 RecoverFromWal 执行时，文件句柄已经完全准备好了。
        // 析构时：先销毁 table_，再销毁 wal_。这保证了在内存索引销毁的过程中，如果还有任何最后的日志要写，wal_ 依然是有效的。
        WalHandler wal_;
        SkipList<K, V> table_;
        mutable  std::shared_mutex rw_lock_; // 读写锁


        // 序列化
        template<typename T>
        std::string Serialize(const T& value) {
            if constexpr (std::is_same_v<T, std::string>) {
                return value;
            } else {
                // 对于 int, double 等定长类型，直接拷贝内存字节
                return std::string(reinterpret_cast<const char*>(&value), sizeof(T));
            }
        }

        template<typename T>
        T Deserialize(const std::string& data) {
            if constexpr (std::is_same_v<T, std::string>) {
                return data;
            } else {
                T value;
                // 将字节拷贝回对象内存
                std::memcpy(&value, data.data(), sizeof(T));
                return value;
            }
        }

        void RecoverFromWal() {
            // 加个写锁
            std::unique_lock<std::shared_mutex> lock(rw_lock_);
            // 调用 WalHandler 的 LoadLog，并传入 Lambda 作为回调
            wal_.LoadLog([this](OpType type, const std::string& k_str, const std::string& v_str) {
                // 1. 将字符串还原为原始的 K 和 V 类型
                K key = Deserialize<K>(k_str);
                V val = Deserialize<V>(v_str);

                // 2. 重放操作
                if (type == OpType::ADD) {
                    table_.insert_element(key, val);
                } else if (type == OpType::DEL) {
                    table_.delete_element(key);
                }
            });
            LOG_DEBUG(std::string("MemTable recovery complete. Loaded ")
                      + std::to_string(table_.size()) + " element(s).");
        }

    public:
        // MemTable(int max_level = 16, const std::string& wal_file) : table_(max_level), wal_(wal_file) {}
        // 当构造函数中既有带默认值的参数，又有必须传递的参数时，C++ 规定：默认实参必须从右向左排列。
        MemTable(const std::string& wal_file, int max_level = 16) : wal_(wal_file), table_(max_level) {
            // 启动时自动恢复
            RecoverFromWal();
        }


        // 插入或更新
        void Put(const K& key, const V& value) {
            // 加个写锁，确保同一时间只有一个线程在修改SkipList
            std::unique_lock<std::shared_mutex> lock(rw_lock_);
            // 关键：先写日志，再改内存！
            wal_.AddLog(Serialize(key), Serialize(value), OpType::ADD);
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
            // 关键：先写日志，再改内存！
            wal_.AddLog(Serialize(key), "", OpType::DEL);
            // 加锁后直接调用delete
            return table_.delete_element(key);
        }

        // 获取当前数量
        int Count() const {
            std::shared_lock<std::shared_mutex> lock(rw_lock_);
            return table_.size();
        }

        auto GetIterator() {
            // 记得加读锁，虽然此时 imm_ 是只读的，但养成习惯没坏处
            return table_.begin();
        }

        // [DBImpl]
        // 1. 获取 WalHandler 指针，方便 DBImpl 调用 LoadLog
        WalHandler* GetWalHandler() { return &wal_; }

        // 2. 纯内存插入：用于从 WAL 恢复数据或 Minor Compaction
        // 绕过了 wal_.AddLog(key, value, OpType::ADD);
        void PutWithoutWal(const K& key, const V& value) {
            std::unique_lock<std::shared_mutex> lock(rw_lock_);
            table_.insert_element(key, value);
        }

        // 3. 纯内存删除
        void RemoveWithoutWal(const K& key) {
            std::unique_lock<std::shared_mutex> lock(rw_lock_);
            table_.delete_element(key);
        }

        // 4. 获取内存占用估算 (字节)
        // 这是一个硬核指标，用于触发 Minor Compaction
        size_t ApproximateMemoryUsage() {
            // 粗略计算：SkipList 节点数 * (Key平均大小 + Value平均大小 + 指针开销)
            // 简单起见，可以先返回 table_.size() * 100 (假设平均每条 100 字节)
            // 或者在 SkipList 里维护一个精确的 byte_counter
            return table_.size() * 128;
        }
};

#endif //NOVAKV_MEMTABLE_H
