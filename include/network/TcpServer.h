//
// Created by 26708 on 2026/3/10.
//

#ifndef NOVAKV_TCPSERVER_H
#define NOVAKV_TCPSERVER_H

#include <cstdint>
#include <unordered_map>

#include "network/CommandExecutor.h"
#include "network/Connection.h"

class TcpServer {
 public:
  explicit TcpServer(DBImpl* db);
  ~TcpServer();

  bool Start(uint16_t port);
  void Run();
  void Stop();

 private:
  bool InitListenSocket(uint16_t port);
  bool InitEpoll();

  bool AddEpollEvent(int fd, uint32_t events);
  bool UpdateEpollEvent(int fd, uint32_t events);
  void RemoveEpollEvent(int fd);

  void HandleAccept();
  void HandleConnectionEvent(int fd, uint32_t events);
  void CloseConnection(int fd);

  static bool SetNonBlocking(int fd);

  CommandExecutor executor_;
  int listen_fd_ = -1;                               // 监听端口用的 socket
  int epoll_fd_ = -1;                                // epoll 实例
  bool running_ = false;                             // 事件循环是否继续运行
  std::unordered_map<int, Connection> connections_;  // 所有在线连接
};

#endif  // NOVAKV_TCPSERVER_H
