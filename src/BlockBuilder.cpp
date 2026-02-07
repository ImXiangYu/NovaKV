//
// Created by 26708 on 2026/2/5.
//

#include "BlockBuilder.h"

void BlockBuilder::Add(const std::string& key, const std::string& value) {
    // 1. 获取长度
    uint32_t key_len = static_cast<uint32_t>(key.size());
    uint32_t val_len = static_cast<uint32_t>(value.size());

    // 2. 将 Key 长度压入缓冲区 (模仿二进制序列化)
    // 指针强转：把 uint32_t 的 4 个字节直接拷贝进 string
    buffer_.append(reinterpret_cast<char*>(&key_len), sizeof(uint32_t));

    // 3. 将 Key 内容压入
    buffer_.append(key);

    // 4. 将 Value 长度压入
    buffer_.append(reinterpret_cast<char*>(&val_len), sizeof(uint32_t));

    // 5. 将 Value 内容压入
    buffer_.append(value);

    // 6. 计数器增加
    counter_++;
}

std::string BlockBuilder::Finish() {
    finished_ = true;
    return buffer_;
}

void BlockBuilder::Reset() {
    buffer_.clear();
    counter_ = 0;
    finished_ = false;
}

size_t BlockBuilder::CurrentSizeEstimate() const {
    return buffer_.size();
}

bool BlockBuilder::Empty() const {
    return buffer_.empty();
}
