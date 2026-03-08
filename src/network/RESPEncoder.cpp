//
// Created by 26708 on 2026/3/8.
//

#include "network/RESPEncoder.h"

void RESPEncoder::EncodeSimpleString(NetworkBuffer* buffer,
                                     const std::string& str) {
  const size_t append_size = 1 + str.size() + 2;
  buffer->EnsureWritableBytes(append_size);
  const std::string append_str = "+" + str + "\r\n";
  buffer->Append(append_str.c_str(), append_size);
}

void RESPEncoder::EncodeError(NetworkBuffer* buffer, const std::string& msg) {
  const size_t append_size = 5 + msg.size() + 2;
  buffer->EnsureWritableBytes(append_size);
  const std::string append_str = "-ERR " + msg + "\r\n";
  buffer->Append(append_str.c_str(), append_size);
}

void RESPEncoder::EncodeInteger(NetworkBuffer* buffer, const int64_t val) {
  const std::string msg = std::to_string(val);
  const size_t append_size = 1 + msg.size() + 2;
  buffer->EnsureWritableBytes(append_size);
  const std::string append_str = ":" + msg + "\r\n";
  buffer->Append(append_str.c_str(), append_size);
}

void RESPEncoder::EncodeBulkString(NetworkBuffer* buffer,
                                   const std::string& str) {
  const size_t prefix_size = 1 + str.size() + 2;
  buffer->EnsureWritableBytes(prefix_size);
  const std::string prefix_str = "$" + std::to_string(prefix_size) + "\r\n";
  buffer->Append(prefix_str.c_str(), prefix_size);

  buffer->EnsureWritableBytes(str.size());
  buffer->Append(str.data(), str.size());

  constexpr size_t suffix_size = 2;
  buffer->EnsureWritableBytes(suffix_size);
  const std::string suffix_str = "\r\n";
  buffer->Append(suffix_str.c_str(), suffix_size);
}

void RESPEncoder::EncodeNull(NetworkBuffer* buffer) {
  constexpr size_t append_size = 5;
  buffer->EnsureWritableBytes(append_size);
  buffer->Append("$-1\r\n", append_size);
}

void RESPEncoder::EncodeArray(NetworkBuffer* buffer,
                              const std::vector<std::string>& elements) {
  const size_t prefix_size = 1 + elements.size() + 2;
  buffer->EnsureWritableBytes(prefix_size);
  const std::string prefix_str = "*" + std::to_string(prefix_size) + "\r\n";
  buffer->Append(prefix_str.c_str(), prefix_size);

  for (const auto& element : elements) {
    EncodeSimpleString(buffer, element);
  }
}