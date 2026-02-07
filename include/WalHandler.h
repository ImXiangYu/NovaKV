//
// Created by 26708 on 2026/2/4.
//

#ifndef NOVAKV_WALHANDLER_H
#define NOVAKV_WALHANDLER_H

#include <cstdint>
#include <functional>
#include <string>

enum class OpType : uint8_t {
    ADD = 1,
    DEL = 2
};

class WalHandler {
    private:
        std::ofstream dest_; // 文件输出流
        std::string filename_;

        // 预计算的 CRC32 表，用于加速计算
        uint32_t CalculateCRC32(const char* data, size_t len);

    public:
        explicit WalHandler(const std::string& filename);
        ~WalHandler();

        // 核心接口：将 KV 操作持久化
        void AddLog(const std::string& key, const std::string& value, OpType type);

        void LoadLog(std::function<void(OpType, const std::string&, const std::string&)> callback);
};

#endif //NOVAKV_WALHANDLER_H
