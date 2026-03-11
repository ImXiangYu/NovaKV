//
// Created by 26708 on 2026/3/10.
//

#ifndef NOVAKV_TCPSERVER_H
#define NOVAKV_TCPSERVER_H

#include <cstdint>
#include <unordered_map>

#include "network/CommandExecutor.h"
#include "network/Connection.h"
#include "network/ThreadPool.h"

struct WorkerTask {
  int fd;
  uint64_t generation;
  std::vector<std::string> command;
};

struct CompletedTask {
  int fd;
  uint64_t generation;
  std::string response;
};

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

  bool AddEpollEvent(int fd, uint32_t events) const;
  bool UpdateEpollEvent(int fd, uint32_t events) const;
  void RemoveEpollEvent(int fd) const;

  void HandleAccept();
  void HandleConnectionEvent(int fd, uint32_t events);
  void CloseConnection(int fd);

  void HandleRead(int fd);
  void HandleWrite(int fd);

  static bool SetNonBlocking(int fd);

  CommandExecutor executor_;
  int listen_fd_ = -1;                               // 监听端口用的 socket
  int epoll_fd_ = -1;                                // epoll 实例
  bool running_ = false;                             // 事件循环是否继续运行
  std::unordered_map<int, Connection> connections_;  // 所有在线连接

  // 初始化唤醒 fd，并注册进 epoll
  bool InitWakeFd();
  // IO 线程处理完成队列的入口
  void HandleWorkerCompletions();

  Ayu::ThreadPool thread_pool_;                // 线程池
  int wake_fd_ = -1;                           // 用来唤醒 epoll_wait 的通知 fd
  uint64_t next_generation_ = 1;               // 给每个新连接分配唯一代次号
  std::mutex completed_mu_;                    // 保护完成队列
  std::queue<CompletedTask> completed_queue_;  // worker 的结果暂存区
};

#endif  // NOVAKV_TCPSERVER_H
