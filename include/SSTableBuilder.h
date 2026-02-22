//
// Created by 26708 on 2026/2/5.
//

#ifndef NOVAKV_SSTABLEBUILDER_H
#define NOVAKV_SSTABLEBUILDER_H

#include <string>
#include <vector>

#include "BlockBuilder.h"
#include "BloomFilter.h"
#include "FileFormats.h"
#include "Storage.h"
#include "ValueRecord.h"

class SSTableBuilder {
 public:
  explicit SSTableBuilder(WritableFile* file);
  ~SSTableBuilder() = default;

  // 核心接口：添加一条数据
  void Add(const std::string& key, const std::string& value, ValueType type);

  // 将内存里剩下的数据全部刷入磁盘，并写下索引和 Footer
  void Finish();

 private:
  void WriteDataBlock();
  void WriteIndexBlock();
  void WriteFilterBlock();

  WritableFile* file_;
  BlockBuilder data_block_;
  std::string last_key_;
  std::vector<IndexEntry> index_entries_;
  std::vector<std::string> keys_;  // 暂存所有加入的 Key，用于生成过滤器
  BlockHandle filter_handle_;      // 记录过滤器在文件中的位置
};

#endif  // NOVAKV_SSTABLEBUILDER_H
