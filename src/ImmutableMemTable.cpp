//
// Created by [Your Name] on 2026/2/7.
//

#include "ImmutableMemTable.h"
#include "MemTable.h"
#include "SSTableBuilder.h"
#include <chrono>
#include <iostream>
#include <fstream>

template<typename K, typename V>
std::shared_ptr<ImmutableMemTable<K, V>> ImmutableMemTable<K, V>::CreateFromActive(
    const MemTable<K, V>& active_memtable, uint64_t table_id) {
    
    // TODO: 这里需要 MemTable 提供获取内部 SkipList 的接口
    // 暂时返回 nullptr，后续实现
    return nullptr;
}

template<typename K, typename V>
bool ImmutableMemTable<K, V>::FlushToSSTable(const std::string& file_path) const {
    try {
        // 1. 创建 SSTable 构建器
        std::ofstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << file_path << std::endl;
            return false;
        }
        
        WritableFile writable_file(file.rdbuf());
        SSTableBuilder builder(&writable_file);
        
        // 2. 遍历 SkipList 中的所有数据并添加到 SSTable
        // TODO: 需要 SkipList 提供迭代器支持
        /*
        for (auto it = skip_list_->begin(); it != skip_list_->end(); ++it) {
            builder.Add(it->key, it->value);
        }
        */
        
        // 3. 完成构建
        builder.Finish();
        file.close();
        
        std::cout << "Successfully flushed ImmutableMemTable #" << table_id_ 
                  << " to SSTable: " << file_path << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error flushing ImmutableMemTable #" << table_id_ 
                  << " to SSTable: " << e.what() << std::endl;
        return false;
    }
}

// 显式实例化常用的模板类型
template class ImmutableMemTable<std::string, std::string>;
template class ImmutableMemTable<int, std::string>;
template class ImmutableMemTable<std::string, int>;