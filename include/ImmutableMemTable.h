//
// Created by 2670 on 2026/2/7.
//

#ifndef NOVAKV_IMMUTABLEMEMTABLE_H
#define NOVAKV_IMMUTABLEMEMTABLE_H

#include <memory>
#include <vector>
#include <string>
#include <shared_mutex>

#include "MemTable.h"
#include "SkipList.h"

/**
 * @brief 不可变内存表（Immutable MemTable）
 * 
 * 核心设计理念：
 * 1. 当活跃 MemTable 达到阈值时，将其转换为 Immutable 状态
 * 2. Immutable MemTable 只支持读操作，不支持写操作
 * 3. 通过后台线程异步刷盘到 SSTable 文件
 * 4. 多个 Immutable MemTable 可以并发存在，形成 Level 0 层
 * 
 * 线程安全性：
 * - 读操作：使用 shared_mutex 的 shared_lock，支持多线程并发读取
 * - 生命周期管理：使用 shared_ptr 确保安全的引用计数
 */
template<typename K, typename V>
class ImmutableMemTable {
private:
    // 使用共享指针包装 SkipList，便于生命周期管理和线程安全
    std::shared_ptr<SkipList<K, V>> skip_list_;
    
    // 只读锁：允许多个读线程并发访问
    mutable std::shared_mutex read_mutex_;
    
    // 唯一标识符，用于区分不同的 Immutable MemTable
    const uint64_t table_id_;
    
    // 记录创建时间，用于后续的 Compaction 策略
    const uint64_t creation_time_;

public:
    /**
     * @brief 构造函数
     * @param skip_list 要包装的 SkipList（通常是来自活跃 MemTable）
     * @param table_id 唯一标识符
     */
    explicit ImmutableMemTable(std::shared_ptr<SkipList<K, V>> skip_list, uint64_t table_id)
        : skip_list_(std::move(skip_list)), 
          table_id_(table_id),
          creation_time_(GetCurrentTimestamp()) {}

    /**
     * @brief 从活跃 MemTable 创建 Immutable MemTable
     * @param active_memtable 活跃的 MemTable 实例
     * @param table_id 唯一标识符
     * @return 新创建的 Immutable MemTable
     */
    static std::shared_ptr<ImmutableMemTable<K, V>> CreateFromActive(
        const MemTable<K, V>& active_memtable, uint64_t table_id);

    /**
     * @brief 查询接口 - 线程安全的只读操作
     * @param key 要查询的键
     * @param value 输出参数，存储查询结果
     * @return 是否找到该键
     */
    bool Get(const K& key, V& value) const {
        // 使用共享锁，允许多线程并发读取
        std::shared_lock<std::shared_mutex> lock(read_mutex_);
        
        // 委托给内部的 SkipList 执行实际查询
        return skip_list_->search_element(key, value);
    }

    /**
     * @brief 获取表中元素数量
     * @return 元素总数
     */
    size_t Size() const {
        std::shared_lock<std::shared_mutex> lock(read_mutex_);
        return skip_list_->size();
    }

    /**
     * @brief 获取唯一标识符
     * @return 表 ID
     */
    uint64_t GetTableId() const { return table_id_; }

    /**
     * @brief 获取创建时间戳
     * @return 创建时间
     */
    uint64_t GetCreationTime() const { return creation_time_; }

    /**
     * @brief 获取内部迭代器，用于刷盘操作
     * @note 此方法仅供内部使用，外部不应直接调用
     */
    typename SkipList<K, V>::Iterator GetIterator() const {
        std::shared_lock<std::shared_mutex> lock(read_mutex_);
        return skip_list_->begin(); // 假设 SkipList 有迭代器支持
    }

    /**
     * @brief 将当前 Immutable MemTable 刷写到 SSTable 文件
     * @param file_path 目标文件路径
     * @return 是否刷写成功
     */
    bool FlushToSSTable(const std::string& file_path) const;

private:
    /**
     * @brief 获取当前时间戳（毫秒）
     * @return 时间戳
     */
    static uint64_t GetCurrentTimestamp() {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }
};

#endif // NOVAKV_IMMUTABLEMEMTABLE_H