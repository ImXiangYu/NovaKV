//
// Created by 26708 on 2026/2/4.
//

#include "WalHandler.h"

#include <array>
#include <fstream>

#include "Logger.h"

uint32_t WalHandler::CalculateCRC32(const char* data, size_t len) {
  static const auto crc_table = []() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t crc = i;
      for (uint32_t j = 0; j < 8; ++j) {
        crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320 : 0);
      }
      table[i] = crc;
    }
    return table;
  }();

  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc = (crc >> 8) ^
          crc_table[(crc ^ static_cast<unsigned char>(data[i])) & 0xFF];
  }
  return crc ^ 0xFFFFFFFF;
}

WalHandler::WalHandler(const std::string& filename) : filename_(filename) {
  // 以二进制、追加模式打开文件
  dest_.open(filename, std::ios::binary | std::ios::app);
  if (!dest_.is_open()) {
    LOG_ERROR(std::string("WAL open failed: ") + filename_);
  } else {
    LOG_INFO(std::string("WAL opened: ") + filename_);
  }
}

WalHandler::~WalHandler() {
  if (dest_.is_open()) {
    dest_.close();
    LOG_INFO(std::string("WAL closed: ") + filename_);
  }
}

std::string WalHandler::GetFilename() const { return filename_; }

void WalHandler::AddLog(const std::string& key, const std::string& value,
                        ValueType type) {
  // 1. 拼凑整个 Body 内容
  std::string payload;
  uint8_t t = static_cast<uint8_t>(type);
  uint32_t k_len = key.size();
  uint32_t v_len = value.size();

  // 预分配空间，减少扩容开销
  payload.reserve(1 + 4 + k_len + 4 + v_len);

  payload.append(reinterpret_cast<char*>(&t), 1);
  payload.append(reinterpret_cast<char*>(&k_len), 4);
  payload.append(key);
  payload.append(reinterpret_cast<char*>(&v_len), 4);
  payload.append(value);

  // 2. 计算 Checksum
  uint32_t crc = CalculateCRC32(payload.c_str(), payload.size());

  // 3. 一次性写入：[CRC (4B)] + [Payload]
  dest_.write(reinterpret_cast<char*>(&crc), 4);
  dest_.write(payload.data(), payload.size());
  dest_.flush();
}

void WalHandler::LoadLog(
    std::function<void(ValueType, const std::string&, const std::string&)>
        callback) {
  std::ifstream src(filename_, std::ios::binary);
  if (!src.is_open()) return;
  while (src.peek() != EOF) {
    // 1. 读取 Checksum
    uint32_t saved_crc;
    if (!src.read(reinterpret_cast<char*>(&saved_crc), 4)) break;

    // 2. 为了校验 CRC，我们需要知道接下来要读多少数据，或者边读边算
    // 在现在的结构下，我们需要逐个字段读取 Body
    // 逻辑：读取 Type(1) + KeyLen(4) -> 读 Key -> 读 ValueLen(4) -> 读 Value
    uint8_t t;
    src.read(reinterpret_cast<char*>(&t), 1);

    uint32_t k_len;
    src.read(reinterpret_cast<char*>(&k_len), 4);
    std::string key(k_len, '\0');
    src.read(&key[0], k_len);

    uint32_t v_len;
    src.read(reinterpret_cast<char*>(&v_len), 4);
    std::string value(v_len, '\0');
    src.read(&value[0], v_len);

    // 3. 校验 CRC（重新构造 Body 进行计算，确保数据没被篡改）
    std::string body;
    body.append(reinterpret_cast<char*>(&t), 1);
    body.append(reinterpret_cast<char*>(&k_len), 4);
    body.append(key);
    body.append(reinterpret_cast<char*>(&v_len), 4);
    body.append(value);

    if (CalculateCRC32(body.data(), body.size()) != saved_crc) {
      LOG_ERROR("WAL checksum mismatch: data might be corrupted.");
      break;  // 或者跳过这条记录
    }

    // 4. 通过回调函数，把恢复出来的 KV 交给 MemTable 处理
    callback(static_cast<ValueType>(t), key, value);
  }
  src.close();
}
