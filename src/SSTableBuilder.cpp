//
// Created by 26708 on 2026/2/6.
//

#include "SSTableBuilder.h"

#include "Logger.h"

SSTableBuilder::SSTableBuilder(WritableFile* file) : file_(file) {}

void SSTableBuilder::Add(const std::string& key, const std::string& value,
                         ValueType type) {
  // 1. 如果当前 BlockBuilder 已经够大了（如 4KB），执行 Flush()
  if (data_block_.CurrentSizeEstimate() >= 4096) {
    WriteDataBlock();
  }

  // 2. 将数据喂给 BlockBuilder
  data_block_.Add(key, value, type);
  // 收集 Key 用于布隆过滤器
  keys_.push_back(key);

  // 3. 更新当前文件的最大 Key
  last_key_ = key;  // 持续更新，直到 Block 结束，它就是 Last Key
}

void SSTableBuilder::Finish() {
  // 1. 刷入最后一个 Data Block
  if (!data_block_.Empty()) {
    WriteDataBlock();
  }

  // 写入布隆过滤器
  WriteFilterBlock();

  // 记录 Index Block 的位置
  BlockHandle index_handle;
  index_handle.offset = file_->Size();

  // 2. 写入 Index Block（目录层）
  WriteIndexBlock();
  index_handle.size = file_->Size() - index_handle.offset;

  // 3. 写入 Footer
  Footer footer;
  footer.index_handle = index_handle;
  footer.filter_handle = filter_handle_;

  std::string footer_encoding;
  footer.EncodeTo(&footer_encoding);
  file_->Append(footer_encoding);

  file_->Flush();
  LOG_INFO(std::string("SSTable build finished. Total size: ") +
           std::to_string(file_->Size()));
}

void SSTableBuilder::WriteDataBlock() {
  // 1. 获取当前块在文件中的偏移量
  BlockHandle handle;
  handle.offset = file_->Size();

  // 2. 结束构建并拿到二进制数据
  std::string content = data_block_.Finish();
  handle.size = content.size();

  // 3. 真正写入文件
  file_->Append(content);

  // 4. 重置 BlockBuilder，为下一个块做准备
  data_block_.Reset();

  // 5. 将这一块的信息记入索引条目（使用当前的 last_key_）
  index_entries_.push_back({last_key_, handle});
  LOG_DEBUG(
      std::string("Flush data block: offset=") + std::to_string(handle.offset) +
      ", size=" + std::to_string(handle.size) + ", last_key=" + last_key_);
}

void SSTableBuilder::WriteIndexBlock() {
  BlockBuilder index_builder;
  for (const auto& entry : index_entries_) {
    // 索引块也是一个 Block，KV 结构为：
    // Key = Data Block 的 Last Key
    // Value = 序列化后的 BlockHandle (offset + size)
    std::string handle_encoding;
    handle_encoding.append(reinterpret_cast<const char*>(&entry.handle.offset),
                           sizeof(uint64_t));
    handle_encoding.append(reinterpret_cast<const char*>(&entry.handle.size),
                           sizeof(uint64_t));

    index_builder.Add(entry.last_key, handle_encoding, ValueType::kValue);
  }

  file_->Append(index_builder.Finish());
  LOG_DEBUG(std::string("Index block written. Entries: ") +
            std::to_string(index_entries_.size()));
}

void SSTableBuilder::WriteFilterBlock() {
  if (keys_.empty()) return;

  BloomFilter filter_gen(10);
  std::string filter_data = filter_gen.CreateFilter(keys_);

  // 直接通过 file_->Size() 获取当前准确的偏移量
  filter_handle_.offset = file_->Size();
  filter_handle_.size = filter_data.size();

  file_->Append(filter_data);
  LOG_DEBUG(std::string("Filter block written. Offset=") +
            std::to_string(filter_handle_.offset) +
            ", size=" + std::to_string(filter_handle_.size));
}
