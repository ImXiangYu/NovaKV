//
// Created by 26708 on 2026/3/10.
//

#include "network/TcpServer.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

#include "Logger.h"
#include "network/RESPEncoder.h"
TcpServer::TcpServer(DBImpl* db) : executor_(db), thread_pool_(8) {}
TcpServer::~TcpServer() {
  Stop();
  Cleanup();
}
bool TcpServer::Start(const uint16_t port) {
  if (!InitListenSocket(port)) {
    return false;
  }

  if (!InitEpoll()) {
    Cleanup();
    return false;
  }

  if (!AddEpollEvent(listen_fd_, EPOLLIN)) {
    Cleanup();
    return false;
  }

  if (!InitWakeFd()) {
    Cleanup();
    return false;
  }

  running_ = true;
  return true;
}
void TcpServer::Run() {
  constexpr int kMaxEvents = 64;
  epoll_event events[kMaxEvents];

  while (running_.load()) {
    const int ready = epoll_wait(epoll_fd_, events, kMaxEvents, -1);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }

    for (int i = 0; i < ready; ++i) {
      if (!running_.load()) {
        break;
      }

      const int fd = events[i].data.fd;
      const uint32_t ev = events[i].events;

      if (fd == listen_fd_) {
        HandleAccept();
      } else if (fd == wake_fd_) {
        HandleWorkerCompletions();
      } else {
        HandleConnectionEvent(fd, ev);
      }
    }
  }
  // 循环结束后CleanUp
  Cleanup();
}
void TcpServer::Stop() {
  const bool was_running = running_.exchange(false);
  if (!was_running) {
    return;
  }

  WakeEventLoop();
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

  // epoll_ctl(... EPOLL_CTL_MOD ...) 是把某个 fd 注册进epoll，
  // 并告诉内核关心什么事件
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
    return false;
  }
  return true;
}
void TcpServer::RemoveEpollEvent(const int fd) const {
  epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
}
void TcpServer::HandleAccept() {
  while (running_.load()) {
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);

    int client_fd = accept(
        listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

    if (client_fd < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      break;
    }

    if (!SetNonBlocking(client_fd)) {
      close(client_fd);
      continue;
    }

    auto [it, inserted] =
        connections_.try_emplace(client_fd, client_fd, next_generation_++);
    if (!inserted) {
      close(client_fd);
      continue;
    }

    if (!AddEpollEvent(client_fd, EPOLLIN | EPOLLRDHUP)) {
      connections_.erase(it);
      close(client_fd);
      continue;
    }
  }
}
void TcpServer::HandleConnectionEvent(const int fd, const uint32_t events) {
  if (!running_.load()) {
    return;
  }

  if (events & (EPOLLERR | EPOLLHUP)) {
    CloseConnection(fd);
    return;
  }

  if (events & EPOLLIN) {
    HandleRead(fd);
  }

  if (events & EPOLLOUT) {
    HandleWrite(fd);
  }
}
void TcpServer::CloseConnection(const int fd) {
  const auto it = connections_.find(fd);
  if (it == connections_.end()) {
    return;
  }

  RemoveEpollEvent(fd);
  close(fd);
  connections_.erase(it);
}
void TcpServer::HandleRead(const int fd) {
  // 找到 Connection
  const auto it = connections_.find(fd);
  if (it == connections_.end()) {
    return;
  }
  Connection& conn = it->second;
  if (conn.closing) {
    return;
  }

  // 做一次 Socket 读取
  int savedErrno = 0;
  const ssize_t n = conn.input_buffer.ReadFromFd(fd, &savedErrno);

  // 对端正常关闭了连接
  // 这是 TCP 的 EOF 语义
  if (n == 0) {
    CloseConnection(fd);
    return;
  }
  // 这时要看 savedErrno
  if (n < 0) {
    // 非阻塞 socket 当前已经没有更多数据可读，这不是错误，只是“这轮读完了”
    if (savedErrno == EAGAIN || savedErrno == EWOULDBLOCK) return;
    // 系统调用被信号打断，中断
    if (savedErrno == EINTR) return;
    // 其他错误，是真实读错误
    CloseConnection(fd);
    return;
  }

  while (true) {
    std::vector<std::string> command;
    const ParseStatus status = conn.parser.Parse(&conn.input_buffer, command);

    if (status == ParseStatus::SUCCESS) {
      const uint64_t seq = conn.next_request_seq++;
      WorkerTask worker_task{fd, conn.generation, seq, std::move(command)};
      try {
        thread_pool_.enqueue([this, worker_task] {
          // worker 里用临时 NetworkBuffer 生成响应
          NetworkBuffer response_buffer;
          try {
            // 临时 NetworkBuffer 用来生成响应
            executor_.Execute(worker_task.command, &response_buffer);
          } catch (const std::exception& e) {
            const std::string errorMsg = e.what();
            LOG_ERROR("internal server error: " + errorMsg);
            RESPEncoder::EncodeError(&response_buffer, "internal server error");
          } catch (...) {
            LOG_ERROR("worker task failed with unknown exception");
            RESPEncoder::EncodeError(&response_buffer, "internal server error");
          }
          {
            // 将结果封装到任务对象中
            const CompletedTask done_task{
                worker_task.fd, worker_task.generation, worker_task.seq,
                response_buffer.RetrieveAllAsString()};
            std::unique_lock lock(completed_mu_);
            completed_queue_.push(done_task);
          }
          constexpr uint64_t one = 1;
          while (true) {
            const ssize_t wake_n = write(this->wake_fd_, &one, sizeof(one));
            if (wake_n == static_cast<ssize_t>(sizeof(one))) {
              break;
            }
            if (wake_n < 0 && errno == EINTR) {
              continue;
            }
            break;
          }
        });
      } catch (const std::exception& e) {
        CloseConnection(fd);
        return;
      }
      continue;
    }
    if (status == ParseStatus::INCOMPLETE) {
      break;
    }
    if (status == ParseStatus::ERROR) {
      FailConnectionProtocol(conn, fd, "protocol error");
      return;
    }
  }
}
void TcpServer::HandleWrite(const int fd) {
  const auto it = connections_.find(fd);
  if (it == connections_.end()) {
    return;
  }

  Connection& conn = it->second;

  while (conn.output_buffer.ReadableBytes() > 0) {
    const ssize_t n = write(fd, conn.output_buffer.Peek(),
                            conn.output_buffer.ReadableBytes());

    if (n > 0) {
      conn.output_buffer.Retrieve(static_cast<size_t>(n));
      continue;
    }

    if (n == 0) {
      break;
    }

    // 系统调用被信号打断
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    }
    CloseConnection(fd);
    return;
  }
  if (conn.output_buffer.ReadableBytes() == 0) {
    if (conn.closing) {
      const bool all_done = conn.pending_responses.empty() &&
                            conn.next_write_seq == conn.next_request_seq;

      if (all_done) {
        CloseConnection(fd);
        return;
      }

      if (!UpdateEpollEvent(fd, EPOLLRDHUP)) {
        CloseConnection(fd);
      }
      return;
    }

    if (!UpdateEpollEvent(fd, EPOLLIN | EPOLLRDHUP)) {
      CloseConnection(fd);
      return;
    }
  }
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
void TcpServer::Cleanup() {
  if (listen_fd_ >= 0) {
    RemoveEpollEvent(listen_fd_);
    close(listen_fd_);
    listen_fd_ = -1;
  }

  thread_pool_.Shutdown();

  {
    std::lock_guard lock(completed_mu_);
    std::queue<CompletedTask> empty;
    std::swap(completed_queue_, empty);
  }

  for (auto& [fd, conn] : connections_) {
    RemoveEpollEvent(fd);
    close(fd);
  }
  connections_.clear();

  if (wake_fd_ >= 0) {
    RemoveEpollEvent(wake_fd_);
    close(wake_fd_);
    wake_fd_ = -1;
  }

  if (epoll_fd_ >= 0) {
    close(epoll_fd_);
    epoll_fd_ = -1;
  }
}
void TcpServer::WakeEventLoop() const {
  if (wake_fd_ < 0) {
    return;
  }

  constexpr uint64_t one = 1;
  while (true) {
    const ssize_t n = ::write(wake_fd_, &one, sizeof(one));
    if (n == static_cast<ssize_t>(sizeof(one))) {
      return;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    return;
  }
}
bool TcpServer::InitWakeFd() {
  wake_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (wake_fd_ < 0) {
    wake_fd_ = -1;
    return false;
  }

  if (!AddEpollEvent(wake_fd_, EPOLLIN)) {
    close(wake_fd_);
    wake_fd_ = -1;
    return false;
  }

  return true;
}
void TcpServer::HandleWorkerCompletions() {
  uint64_t counter = 0;
  while (true) {
    const ssize_t n = read(wake_fd_, &counter, sizeof(counter));
    if (n == sizeof(counter)) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      break;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    break;
  }

  // 处理完成队列
  std::queue<CompletedTask> local;
  {
    std::lock_guard lock(completed_mu_);
    std::swap(local, completed_queue_);
  }

  while (!local.empty()) {
    CompletedTask task = std::move(local.front());
    local.pop();

    auto it = connections_.find(task.fd);
    if (it == connections_.end()) {
      continue;
    }

    Connection& conn = it->second;
    if (conn.generation != task.generation) {
      continue;
    }

    conn.pending_responses.emplace(task.seq, std::move(task.response));
    bool appended = false;
    while (true) {
      auto ready_it = conn.pending_responses.find(conn.next_write_seq);
      if (ready_it == conn.pending_responses.end()) {
        break;
      }

      conn.output_buffer.Append(ready_it->second.data(),
                                ready_it->second.size());
      conn.pending_responses.erase(ready_it);
      ++conn.next_write_seq;
      appended = true;
    }
    if (appended) {
      const uint32_t events = conn.closing ? (EPOLLOUT | EPOLLRDHUP)
                                           : (EPOLLIN | EPOLLRDHUP | EPOLLOUT);
      if (!UpdateEpollEvent(task.fd, events)) {
        CloseConnection(task.fd);
      }
    }
  }
}
void TcpServer::FailConnectionProtocol(Connection& conn, const int fd,
                                       const std::string& msg) {
  NetworkBuffer response_buffer;
  RESPEncoder::EncodeError(&response_buffer, msg);

  const uint64_t seq = conn.next_request_seq++;
  conn.pending_responses.emplace(seq, response_buffer.RetrieveAllAsString());
  conn.closing = true;
  conn.input_buffer.RetrieveAll();

  bool appended = false;
  while (true) {
    auto it = conn.pending_responses.find(conn.next_write_seq);
    if (it == conn.pending_responses.end()) {
      break;
    }
    conn.output_buffer.Append(it->second.data(), it->second.size());
    conn.pending_responses.erase(it);
    ++conn.next_write_seq;
    appended = true;
  }

  if (const uint32_t events = appended ? (EPOLLOUT | EPOLLRDHUP) : EPOLLRDHUP;
      !UpdateEpollEvent(fd, events)) {
    CloseConnection(fd);
  }
}
