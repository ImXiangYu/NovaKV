//
// Created by 26708 on 2026/2/6.
//

#ifndef NOVAKV_SSTABLEREADER_H
#define NOVAKV_SSTABLEREADER_H
#include <functional>
#include <string>
#include <vector>

#include "Storage.h"
#include "ValueRecord.h"

class SSTableReader {
 public:
  // 静态工厂方法：执行文件打开、mmap 和魔数校验
  // 成功返回指针，失败返回 nullptr
  static SSTableReader* Open(const std::string& filename);

  ~SSTableReader();

  // 查询 Key
  bool Get(const std::string& key, std::string* value);
  // 类型感知 Get
  bool GetRecord(const std::string& key, ValueRecord* record);

  // 遍历/导出
  void ForEach(const std::function<void(const std::string&, const std::string&,
                                        ValueType)>& cb) const;

 private:
  // 私有构造函数，防止外部直接 new
  SSTableReader();

  // 内部读取逻辑
  bool ReadFooter();
  bool ReadIndexBlock();
  bool ReadFilterBlock();

  // 资源句柄
  int fd_;            // 文件描述符
  void* data_;        // mmap 映射后的起始地址
  size_t file_size_;  // 文件大小

  Footer footer_;                          // 存放在末尾读到的罗盘信息
  std::vector<IndexEntry> index_entries_;  // 内存中的索引“地图”
  std::string filter_data_;                // 存放从文件中读取的位图数据
};

#endif  // NOVAKV_SSTABLEREADER_H
