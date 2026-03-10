//
// Created by 26708 on 2026/3/10.
//

#ifndef NOVAKV_CONNECTION_H
#define NOVAKV_CONNECTION_H

#include "network/NetworkBuffer.h"
#include "network/RESPParser.h"

struct Connection {
  explicit Connection(const int client_fd) : fd(client_fd) {}

  int fd;                       // 客户端的 socket
  NetworkBuffer input_buffer;   // 读到但还没完全解析完的数据
  NetworkBuffer output_buffer;  // 已经生成但还没完全发出去的响应
  RESPParser parser;            // 这个连接自己的 RESP 解析状态机
  bool closing = false;         // 这个连接是不是准备关闭
};

#endif  // NOVAKV_CONNECTION_H
