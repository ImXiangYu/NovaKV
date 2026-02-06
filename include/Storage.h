//
// Created by 26708 on 2026/2/5.
//
// 定义整个存储层通用的“协议数据结构”和“常量”。

#ifndef NOVAKV_STORAGE_H
#define NOVAKV_STORAGE_H

#include <string>
#include <cstdint>
#include <cstring>

// 就像书的页码：记录这一页从哪开始，多长
struct BlockHandle {
    uint64_t offset;
    uint64_t size;

    BlockHandle() : offset(0), size(0) {}
};

// 索引项：这块地盘最大的 Key 是谁，在哪能找到它
struct IndexEntry {
    std::string last_key;
    BlockHandle handle;
};

// 尾部：它是一个固定长度的结构，永远位于 SSTable 文件的最后几十个字节。
// Index Handle：记录 Index Block 的 offset(8字节) 和 size(8字节)。
// Magic Number：一个 8 字节的随机数（魔数），用来确认这到底是不是一个 NovaKV 的存储文件。
struct Footer {
    inline static const uint64_t kMagicNumber = 0xDEADC0DEFA112026; // 你的专属魔数
    inline static const size_t kEncodedLength = 16 + 16 + 8; // 2个uint64 + 1个magic

    BlockHandle index_handle;
    BlockHandle filter_handle;

    // 序列化：结构体 -> 字节流
    void EncodeTo(std::string* dst) const {
        // [Index handle] 8 字节的 offset
        dst->append(reinterpret_cast<const char*>(&index_handle.offset), sizeof(uint64_t));
        // [Index handle] 8 字节的 size
        dst->append(reinterpret_cast<const char*>(&index_handle.size), sizeof(uint64_t));

        // [Filter Handle] 8 字节的 offset
        dst->append(reinterpret_cast<const char*>(&filter_handle.offset), sizeof(uint64_t));
        // [Filter Handle] 8 字节的 size
        dst->append(reinterpret_cast<const char*>(&filter_handle.size), sizeof(uint64_t));

        // 8 字节的 MagicNumber
        dst->append(reinterpret_cast<const char*>(&kMagicNumber), sizeof(uint64_t));
    }

    // 反序列化：字节流 -> 结构体
    bool DecodeFrom(const std::string& input) {
        if (input.size() < kEncodedLength) return false;

        const char* p = input.data();
        std::memcpy(&index_handle.offset, p, sizeof(uint64_t));
        std::memcpy(&index_handle.size, p + 8, sizeof(uint64_t));

        std::memcpy(&filter_handle.offset, p + 16, 8);
        std::memcpy(&filter_handle.size, p + 24, 8);

        uint64_t magic;
        std::memcpy(&magic, p + 32, sizeof(uint64_t));

        return (magic == kMagicNumber); // 校验魔数
    }
};

#endif //NOVAKV_STORAGE_H