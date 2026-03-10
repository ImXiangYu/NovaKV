//
// Created by 26708 on 2026/3/10.
//

#include "network/TcpServer.h"

#include <unistd.h>
TcpServer::TcpServer(DBImpl* db) : executor_(db) {}
TcpServer::~TcpServer() { Stop(); }
bool TcpServer::Start(uint16_t port) {
  if (!InitListenSocket(port)) {
    return false;
  }

  if (!InitEpoll()) {
    Stop();
    return false;
  }

  if (!AddEpollEvent(listen_fd_, 0)) {
    Stop();
    return false;
  }

  running_ = true;
  return true;
}
void TcpServer::Stop() {
  running_ = false;

  for (auto& [fd, conn] : connections_) {
    close(fd);
  }
  connections_.clear();

  if (epoll_fd_ >= 0) {
    close(epoll_fd_);
    epoll_fd_ = -1;
  }
  if (listen_fd_ >= 0) {
    close(listen_fd_);
    listen_fd_ = -1;
  }
}
