//
// Created by 26708 on 2026/3/8.
//

#include "network/RESPEncoder.h"

void RESPEncoder::EncodeSimpleString(NetworkBuffer* buffer,
                                     const std::string& str) {
  buffer->Append("+", 1);
  buffer->Append(str.data(), str.size());
  buffer->Append(kCRLF, 2);
}

void RESPEncoder::EncodeError(NetworkBuffer* buffer, const std::string& msg) {
  buffer->Append("-ERR ", 5);
  buffer->Append(msg.data(), msg.size());
  buffer->Append(kCRLF, 2);
}

void RESPEncoder::EncodeInteger(NetworkBuffer* buffer, const int64_t val) {
  const std::string s = std::to_string(val);
  buffer->Append(":", 1);
  buffer->Append(s.data(), s.size());
  buffer->Append(kCRLF, 2);
}

void RESPEncoder::EncodeBulkString(NetworkBuffer* buffer,
                                   const std::string& str) {
  const std::string len_str = std::to_string(str.size());
  buffer->Append("$", 1);
  buffer->Append(len_str.data(), len_str.size());
  buffer->Append(kCRLF, 2);

  buffer->Append(str.data(), str.size());
  buffer->Append(kCRLF, 2);
}

void RESPEncoder::EncodeNull(NetworkBuffer* buffer) {
  buffer->Append("$-1", 3);
  buffer->Append(kCRLF, 2);
}

void RESPEncoder::EncodeArray(NetworkBuffer* buffer,
                              const std::vector<std::string>& elements) {
  const std::string len_str = std::to_string(elements.size());
  buffer->Append("*", 1);
  buffer->Append(len_str.data(), len_str.size());
  buffer->Append(kCRLF, 2);

  for (const auto& element : elements) {
    // Array 元素通常使用 BulkString 以保证二进制安全
    EncodeBulkString(buffer, element);
  }
}
