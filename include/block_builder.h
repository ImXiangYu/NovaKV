//
// Created by 26708 on 2026/2/5.
//

#ifndef NOVAKV_BLOCK_BUILDER_H
#define NOVAKV_BLOCK_BUILDER_H

#include <string>
#include <cstdint>
#include <vector>

class BlockBuilder {
    public:
        BlockBuilder() = default;

        /**
         * @brief 添加一个键值对到缓冲区
         * 布局：[KeyLen (4B)] [Key 内容] [ValueLen (4B)] [Value 内容]
         */
        void Add(const std::string& key, const std::string& value) {
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

        /**
         * @brief 完成当前块的构建
         * 在这一版中，我们直接返回 buffer。
         * 未来如果需要对齐 LevelDB，我们要在这里追加“重启点”信息。
         */
        std::string Finish() {
            finished_ = true;
            return buffer_;
        }

        /**
         * @brief 重置 Builder，清空缓冲区，准备构建下一个 Block
         */
        void Reset() {
            buffer_.clear();
            counter_ = 0;
            finished_ = false;
        }

        /**
         * @brief 估算当前缓冲区占用的字节数
         * 用于判断是否达到了 4KB 的阈值，从而触发 Flush 落盘
         */
        size_t CurrentSizeEstimate() const {
            return buffer_.size();
        }

        bool Empty() const {
            return buffer_.empty();
        }

    private:
        std::string buffer_;      // 实际存储二进制数据的容器
        int counter_ = 0;         // 记录存了多少条记录
        bool finished_ = false;   // 状态标记
};

#endif //NOVAKV_BLOCK_BUILDER_H