//
// Created by 26708 on 2026/3/7.
//

#include "network/RESPParser.h"

#include <charconv>

// 从 buffer 解析数据，存入 out_command
ParseStatus RESPParser::Parse(NetworkBuffer* buffer,
                              std::vector<std::string>& out_command) {
  while (true) {
    if (buffer->ReadableBytes() == 0) return ParseStatus::INCOMPLETE;
    switch (state_) {
      case State::EXPECT_ARRAY_SIZE: {
        // *2\r\n$3\r\nGET\r\n$1\r\na\r\n
        // 1. 找 \r\n
        const char* crlf = buffer->FindCRLF();
        if (crlf == nullptr) {
          // 数据不完整
          return ParseStatus::INCOMPLETE;
        }
        // 2. 检查首字节是否为 *
        const char* firstPos = buffer->Peek();

        if (firstPos == crlf) {
          buffer->Retrieve(2);
          continue;
        }

        if (*firstPos != '*') {
          return ParseStatus::ERROR;
        }
        // 3. 提取数字，设置 array_size_
        // 提取*后的，crlf之前的
        auto [ptr, ec] = std::from_chars(firstPos + 1, crlf, array_size_);
        if (ec != std::errc() || ptr != crlf) {
          return ParseStatus::ERROR;
        }

        if (array_size_ < 0) {
          return ParseStatus::ERROR;
        }

        if (array_size_ == 0) {
          buffer->Retrieve(crlf - firstPos + 2);
          Reset();
          return ParseStatus::SUCCESS;
        }
        // 4. 状态跳至 EXPECT_BULK_SIZE
        // 先Retrieve
        buffer->Retrieve(crlf - firstPos + 2);
        state_ = State::EXPECT_BULK_SIZE;
        break;
      }
      case State::EXPECT_BULK_SIZE: {
        // 1. 找 \r\n
        const char* crlf = buffer->FindCRLF();
        if (crlf == nullptr) {
          // 数据不完整
          return ParseStatus::INCOMPLETE;
        }
        // 2. 检查首字节是否为 $
        const char* firstPos = buffer->Peek();

        if (firstPos == crlf) {
          buffer->Retrieve(2);
          continue;
        }

        if (*firstPos != '$') {
          return ParseStatus::ERROR;
        }
        // 3. 提取长度 bulk_len_
        auto [ptr, ec] = std::from_chars(firstPos + 1, crlf, bulk_len_);
        if (ec != std::errc() || ptr != crlf) {
          return ParseStatus::ERROR;
        }

        if (bulk_len_ < -1) {
          return ParseStatus::ERROR;
        }

        if (bulk_len_ == -1) {
          buffer->Retrieve(crlf - firstPos + 2);
          out_command.emplace_back("");

          if (out_command.size() == static_cast<size_t>(array_size_)) {
            Reset();
            return ParseStatus::SUCCESS;
          }

          state_ = State::EXPECT_BULK_SIZE;
          continue;
        }
        // 4. 状态跳至 EXPECT_BULK_DATA
        // 先Retrieve
        buffer->Retrieve(crlf - firstPos + 2);
        state_ = State::EXPECT_BULK_DATA;
        break;
      }
      case State::EXPECT_BULK_DATA: {
        // 1. 检查 buffer 可读字节是否够 bulk_len_ + 2 (\r\n)
        const auto data_len = static_cast<size_t>(bulk_len_);
        if (buffer->ReadableBytes() < static_cast<size_t>(bulk_len_ + 2)) {
          // 数据不完整
          return ParseStatus::INCOMPLETE;
        }

        // 检查是否是\r\n
        const char* firstPos = buffer->Peek();
        if (firstPos[data_len] != '\r' || firstPos[data_len + 1] != '\n') {
          return ParseStatus::ERROR;
        }

        // 2. 提取数据，存入 out_command
        // 读出 bulk_len_ 个字节
        out_command.emplace_back(firstPos, static_cast<size_t>(bulk_len_));
        buffer->Retrieve(bulk_len_ + 2);
        // 3. 检查是否解析完所有参数，完事了就返回 SUCCESS
        if (out_command.size() == static_cast<size_t>(array_size_)) {
          Reset();
          return ParseStatus::SUCCESS;
        } else {
          state_ = State::EXPECT_BULK_SIZE;
        }
        // 没结束就继续
        break;
      }
      default: {
        return ParseStatus::ERROR;
      }
    }
  }
}
void RESPParser::Reset() {
  // 完整解析后，一切恢复到初始状态
  state_ = State::EXPECT_ARRAY_SIZE;
  array_size_ = 0;
  bulk_len_ = 0;
}