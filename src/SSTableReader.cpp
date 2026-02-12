//
// Created by 26708 on 2026/2/6.
//
#include "SSTableReader.h"

#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <algorithm>

#include "BloomFilter.h"
#include "Logger.h"

SSTableReader::SSTableReader(): fd_(-1), data_(MAP_FAILED), file_size_(0) {}

SSTableReader::~SSTableReader() {
    if (data_ != MAP_FAILED) {
        munmap(data_, file_size_);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
}

SSTableReader* SSTableReader::Open(const std::string &filename) {
    // 1. 打开文件 (POSIX 标准)
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd < 0) {
        LOG_ERROR(std::string("Failed to open file: ") + filename);
        return nullptr;
    }

    // 2. 获取文件大小
    struct stat st{};
    if (fstat(fd, &st) != 0 || st.st_size < Footer::kEncodedLength) {
        LOG_ERROR(std::string("Invalid SSTable size: ") + filename);
        close(fd);
        return nullptr;
    }
    size_t size = st.st_size;
    LOG_DEBUG(std::string("SSTable file size: ") + std::to_string(size));

    // 3. 内存映射 (mmap)
    void* mmap_ptr = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mmap_ptr == MAP_FAILED) {
        LOG_ERROR(std::string("mmap failed: ") + filename);
        close(fd);
        return nullptr;
    }
    LOG_DEBUG(std::string("mmap ok: ") + filename);

    // 4. 创建实例并初始化基本成员
    auto* reader = new SSTableReader();
    reader->fd_ = fd;
    reader->data_ = mmap_ptr;
    reader->file_size_ = size;

    // 5. 校验 Footer (这是进入 SSTable 世界的入场券)
    if (!reader->ReadFooter()) {
        LOG_ERROR(std::string("Invalid SSTable file (footer check failed): ") + filename);
        delete reader; // 析构函数会负责 munmap 和 close
        return nullptr;
    }
    LOG_DEBUG(std::string("Footer decoded: ") + filename);

    // 6. 加载索引
    if (!reader->ReadIndexBlock()) {
        LOG_ERROR(std::string("Failed to read index block: ") + filename);
        delete reader;
        return nullptr;
    }
    LOG_DEBUG(std::string("Index block entries: ") + std::to_string(reader->index_entries_.size()));

    // 7. 加载布隆过滤器
    if (!reader->ReadFilterBlock()) {
        LOG_WARN(std::string("Failed to read filter block: ") + filename);
        // 这里可以报错也可以不报错，取决于你是否允许没有过滤器的文件存在
    }

    return reader;
}

// 内部读取逻辑
bool SSTableReader::ReadFooter() {
    // 逻辑：定位到内存末尾的 24 字节
    const char* footer_ptr = static_cast<const char*>(data_) + file_size_ - Footer::kEncodedLength;

    // 将这 24 字节转为 string 供 DecodeFrom 使用
    std::string footer_buf(footer_ptr, Footer::kEncodedLength);

    return footer_.DecodeFrom(footer_buf);
}

bool SSTableReader::ReadIndexBlock() {
    // 1. 获取 Index Block 的位置信息
    uint64_t offset = footer_.index_handle.offset;
    uint64_t size = footer_.index_handle.size;

    // 边界安全检查：索引块不能超出文件范围
    if (offset + size > file_size_ - Footer::kEncodedLength) {
        return false;
    }

    // 2. 定位到索引块起始指针
    const char* index_ptr = static_cast<const char*>(data_) + offset;

    // 3. 解析二进制数据
    // 提示：我们在写入时是按照 [KeyLen][Key][ValueType][Offset][Size] 循环写入的
    uint64_t pos = 0;
    while (pos < size) {
        IndexEntry entry;

        // 读取 Key 长度 (uint32_t)
        uint32_t key_len;
        std::memcpy(&key_len, index_ptr + pos, sizeof(uint32_t));
        pos += sizeof(uint32_t);

        // 读取 Key 字符串
        entry.last_key.assign(index_ptr + pos, key_len);
        pos += key_len;

        // 读取 ValueType
        uint8_t type;
        std::memcpy(&type, index_ptr + pos, sizeof(uint8_t));
        pos += sizeof(uint8_t);

        // 读取 Value 长度 (uint32_t)，其实就是一个标准BlockHandle
        uint32_t val_len;
        std::memcpy(&val_len, index_ptr + pos, sizeof(uint32_t));
        pos += sizeof(uint32_t);

        // 读取 BlockHandle (Offset 和 Size)
        // 读取 Offset
        memcpy(&entry.handle.offset, index_ptr + pos, sizeof(uint64_t));
        pos += sizeof(uint64_t);
        // 读取 Size
        memcpy(&entry.handle.size, index_ptr + pos, sizeof(uint64_t));
        pos += sizeof(uint64_t);

        // 添加到索引列表中
        index_entries_.push_back(entry);
    }

    // 索引列表非空
    return !index_entries_.empty();
}

bool SSTableReader::Get(const std::string &key, std::string *value) {
    // 如果过滤器说肯定不在，直接返回 false，省去后面的索引查找和数据块解析
    if (!filter_data_.empty()) {
        if (!BloomFilter::KeyMayMatch(key, filter_data_)) {
            LOG_DEBUG(std::string("BloomFilter blocked key: ") + key);
            return false;
        }
    }

    // 1. 在索引中二分查找 (使用 std::lower_bound)
    // 查找第一个 last_key >= key 的索引条目
    auto it = std::lower_bound(index_entries_.begin(), index_entries_.end(), key,
        [](const IndexEntry& entry, const std::string& k) {
            return entry.last_key < k;
        });

    // 如果没找到符合条件的 Block，说明 key 大于文件中所有的 key
    if (it == index_entries_.end()) {
        return false;
    }

    // 2. 根据索引条目定位到具体的 Data Block
    const char* block_ptr = static_cast<const char*>(data_) + it->handle.offset;
    uint64_t block_size = it->handle.size;

    // 3. 在 Data Block 内部进行扫描
    // Data Block 布局: [KeyLen][Key][ValLen][Val] ...
    uint64_t pos = 0;
    while (pos < block_size) {
        // 解析 KeyLen
        uint32_t curr_key_len;
        memcpy(&curr_key_len, block_ptr + pos, sizeof(uint32_t));
        pos += sizeof(uint32_t);

        // 解析 Key
        std::string curr_key(block_ptr + pos, curr_key_len);
        pos += curr_key_len;

        // 解析ValueType
        uint8_t vtype;
        memcpy(&vtype, block_ptr + pos, sizeof(uint8_t));
        const auto type = static_cast<ValueType>(vtype);
        pos += sizeof(uint8_t);

        // 解析 ValLen
        uint32_t val_len;
        memcpy(&val_len, block_ptr + pos, sizeof(uint32_t));
        pos += sizeof(uint32_t);

        // 匹配检查
        if (curr_key == key) {
            if (type == ValueType::kDeletion) return false;
            value->assign(block_ptr + pos, val_len);
            return true; // 找到了！
        }

        // 没找着，跳过当前 Value 继续扫下一个 KV
        pos += val_len;
    }

    // 这个 Block 里没有这个 Key
    return false;
}

bool SSTableReader::ReadFilterBlock() {
    uint64_t offset = footer_.filter_handle.offset;
    uint64_t size = footer_.filter_handle.size;

    if (size == 0) return true; // 如果大小为0，说明没写过滤器，正常返回

    // 安全检查
    if (offset + size > file_size_) return false;

    // 直接从 mmap 内存中 copy 出来，或者用 string_view 指向它
    const char* filter_ptr = static_cast<const char*>(data_) + offset;
    filter_data_.assign(filter_ptr, size);

    return true;
}
void SSTableReader::ForEach(const std::function<void(const std::string&, const std::string&, ValueType)>& cb) const {
    for (const auto& entry : index_entries_) {
        uint64_t pos = 0;
        const uint64_t block_size = entry.handle.size;
        const char* block_ptr = static_cast<const char*>(data_) + entry.handle.offset;

        while (pos < block_size) {
            if (pos + sizeof(uint32_t) > block_size) break;
            uint32_t key_len;
            std::memcpy(&key_len, block_ptr + pos, sizeof(uint32_t));
            pos += sizeof(uint32_t);

            if (pos + key_len > block_size) break;
            std::string key(block_ptr + pos, key_len);
            pos += key_len;

            if (pos + sizeof(uint8_t) > block_size) break;
            uint8_t vtype;
            memcpy(&vtype, block_ptr + pos, sizeof(uint8_t));
            const auto type = static_cast<ValueType>(vtype);
            pos += sizeof(uint8_t);

            if (pos + sizeof(uint32_t) > block_size) break;
            uint32_t val_len;
            std::memcpy(&val_len, block_ptr + pos, sizeof(uint32_t));
            pos += sizeof(uint32_t);

            if (pos + val_len > block_size) break;
            std::string value(block_ptr + pos, val_len);
            pos += val_len;

            if (type == ValueType::kValue) {
                cb(key, value, type);
            }
        }
    }
}



