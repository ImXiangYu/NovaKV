//
// Created by 26708 on 2026/3/7.
//

#ifndef NOVAKV_RESPPARSER_H
#define NOVAKV_RESPPARSER_H

#include <string>

#include "network/NetworkBuffer.h"

enum class ParseStatus {
  SUCCESS,     // 解析出一个完整命令
  INCOMPLETE,  // 数据不足，等待下一次读取
  ERROR        // 协议错误
};

class RESPParser {
 public:
  enum class State {
    EXPECT_ARRAY_SIZE,  // 等待 *<len>\r\n
    EXPECT_BULK_SIZE,   // 等待 $<len>\r\n
    EXPECT_BULK_DATA    // 等待 <data>\r\n
  };
  RESPParser()
      : state_(State::EXPECT_ARRAY_SIZE), array_size_(0), bulk_len_(0) {}
  ~RESPParser();

  // 从 buffer 解析数据，存入 out_command
  ParseStatus Parse(NetworkBuffer* buffer,
                    std::vector<std::string>& out_command);

  // 状态重置，用于一个完整命令解析后
  void Reset();

 private:
  State state_;
  size_t array_size_;  // 数组总元素数
  size_t bulk_len_;    // 当前正在解析的字符串长度
};

#endif  // NOVAKV_RESPPARSER_H
