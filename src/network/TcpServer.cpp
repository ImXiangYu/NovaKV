//
// Created by 26708 on 2026/3/10.
//

#include "network/TcpServer.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
TcpServer::TcpServer(DBImpl* db) : executor_(db) {}
TcpServer::~TcpServer() { Stop(); }
bool TcpServer::Start(const uint16_t port) {
  if (!InitListenSocket(port)) {
    return false;
  }

  if (!InitEpoll()) {
    Stop();
    return false;
  }

  if (!AddEpollEvent(listen_fd_, EPOLLIN)) {
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
bool TcpServer::InitListenSocket(const uint16_t port) {
  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    return false;
  }

  constexpr int opt = 1;
  if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  if (!SetNonBlocking(listen_fd_)) {
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

  if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  if (listen(listen_fd_, SOMAXCONN) < 0) {
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  return true;
}
bool TcpServer::InitEpoll() {
  // epoll_create1(0) 是向内核申请一个 epoll 实例，后面所有 fd 都挂到它上面
  epoll_fd_ = epoll_create1(0);
  if (epoll_fd_ < 0) {
    epoll_fd_ = -1;
    return false;
  }
  return true;
}

bool TcpServer::AddEpollEvent(const int fd, const uint32_t events) const {
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd;

  // epoll_ctl(... EPOLL_CTL_ADD ...) 是把某个 fd 注册进epoll，
  // 并告诉内核关心什么事件
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
    return false;
  }
  return true;
}
bool TcpServer::UpdateEpollEvent(const int fd, const uint32_t events) const {
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd;

  // epoll_ctl(... EPOLL_CTL_ADD ...) 是把某个 fd 注册进epoll，
  // 并告诉内核关心什么事件
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
    return false;
  }
  return true;
}
void TcpServer::RemoveEpollEvent(const int fd) const {
  epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
}
bool TcpServer::SetNonBlocking(const int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    return false;
  }
  return true;
}
