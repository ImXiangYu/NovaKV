//
// Created by 26708 on 2026/3/10.
//

#ifndef NOVAKV_CONNECTION_H
#define NOVAKV_CONNECTION_H

#include <cstdint>

#include "network/NetworkBuffer.h"
#include "network/RESPParser.h"

struct Connection {
  explicit Connection(const int client_fd, const uint64_t client_generation)
      : fd(client_fd), generation(client_generation) {}

  int fd;                       // 客户端的 socket
  uint64_t generation = 0;      // 客户端的代次号，用于辨识前后是否是同一个连接
  NetworkBuffer input_buffer;   // 读到但还没完全解析完的数据
  NetworkBuffer output_buffer;  // 已经生成但还没完全发出去的响应
  RESPParser parser;            // 这个连接自己的 RESP 解析状态机
  bool closing = false;         // 这个连接是不是准备关闭
};

#endif  // NOVAKV_CONNECTION_H
