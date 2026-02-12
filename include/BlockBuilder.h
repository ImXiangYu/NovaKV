//
// Created by 26708 on 2026/2/5.
//

#ifndef NOVAKV_BLOCK_BUILDER_H
#define NOVAKV_BLOCK_BUILDER_H

#include <cstdint>
#include <string>

#include "ValueRecord.h"

class BlockBuilder {
    public:
        BlockBuilder() = default;

        /**
         * @brief 添加一个键值对到缓冲区
         * 布局：[KeyLen (4B)] [Key 内容] [ValueType(1B)] [ValueLen (4B)] [Value 内容]
         */
        void Add(const std::string& key, const std::string& value, ValueType type);

        /**
         * @brief 完成当前块的构建
         * 在这一版中，我们直接返回 buffer。
         * 未来如果需要对齐 LevelDB，我们要在这里追加“重启点”信息。
         */
        std::string Finish();

        /**
         * @brief 重置 Builder，清空缓冲区，准备构建下一个 Block
         */
        void Reset();

        /**
         * @brief 估算当前缓冲区占用的字节数
         * 用于判断是否达到了 4KB 的阈值，从而触发 Flush 落盘
         */
        size_t CurrentSizeEstimate() const;

        bool Empty() const;

    private:
        std::string buffer_;      // 实际存储二进制数据的容器
        int counter_ = 0;         // 记录存了多少条记录
        bool finished_ = false;   // 状态标记
};

#endif //NOVAKV_BLOCK_BUILDER_H
