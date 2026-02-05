//
// Created by 26708 on 2026/2/5.
//

#ifndef NOVAKV_SSTABLEBUILDER_H
#define NOVAKV_SSTABLEBUILDER_H

#include "BlockBuilder.h"
#include "FileFormats.h"
#include "Storage.h"
#include <iostream>

class SSTableBuilder {
    public:
        SSTableBuilder(WritableFile* file) : file_(file) {}

        // 核心接口：添加一条数据
        void Add(const std::string& key, const std::string& value) {
            // 1. 如果当前 BlockBuilder 已经够大了（如 4KB），执行 Flush()
            if (data_block_.CurrentSizeEstimate() >= 4096) {
                WriteDataBlock();
            }

            // 2. 将数据喂给 BlockBuilder
            data_block_.Add(key, value);

            // 3. 更新当前文件的最大 Key
            last_key_ = key; // 持续更新，直到 Block 结束，它就是 Last Key
        }

        // 将内存里剩下的数据全部刷入磁盘，并写下索引和 Footer
        void Finish() {
            // 1. 刷入最后一个 Data Block
            if (!data_block_.Empty()) {
                WriteDataBlock();
            }

            // 记录 Index Block 的位置
            BlockHandle index_handle;
            index_handle.offset = file_->Size();

            // 2. 写入 Index Block（目录层）
            WriteIndexBlock();
            index_handle.size = file_->Size() - index_handle.offset;

            // 3. 写入 Footer
            Footer footer;
            footer.index_handle = index_handle;
            std::string footer_encoding;
            footer.EncodeTo(&footer_encoding);
            file_->Append(footer_encoding);

            file_->Flush();
        }

    private:
        void WriteDataBlock() {
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

            std::cout << "Flush Data Block: Offset=" << handle.offset
                      << ", Size=" << handle.size
                      << ", LastKey=" << last_key_ << std::endl;
        }

        void WriteIndexBlock() {
            BlockBuilder index_builder;
            for (const auto& entry : index_entries_) {
                // 索引块也是一个 Block，KV 结构为：
                // Key = Data Block 的 Last Key
                // Value = 序列化后的 BlockHandle (offset + size)
                std::string handle_encoding;
                handle_encoding.append(reinterpret_cast<const char*>(&entry.handle.offset), sizeof(uint64_t));
                handle_encoding.append(reinterpret_cast<const char*>(&entry.handle.size), sizeof(uint64_t));

                index_builder.Add(entry.last_key, handle_encoding);
            }

            file_->Append(index_builder.Finish());
            std::cout << "Index Block Written. Entries: " << index_entries_.size() << std::endl;
        }

        WritableFile* file_;
        BlockBuilder data_block_;
        std::string last_key_;
        std::vector<IndexEntry> index_entries_;
};

#endif //NOVAKV_SSTABLEBUILDER_H