# Linux 高性能网络编程：Epoll 与 Reactor 模式指南

## 1. 核心背景：从阻塞到多路复用

在传统的网络编程模型中，每个连接通常由一个独立的线程处理（Thread-per-Connection）。这种模型在连接数较少时逻辑简单，但面对高并发（C10K
问题）时，会因为线程上下文切换和内存占用导致服务器崩溃。

**IO 多路复用 (IO Multiplexing)** 解决了这一问题：它允许一个线程通过一个系统调用同时监控多个文件描述符（Socket）的状态。

## 2. Epoll 机制详解

`epoll` 是 Linux 下性能最强的 IO 多路复用机制，其核心由三个系统调用组成：

### 2.1 `epoll_create`

创建一个 epoll 实例，返回一个文件描述符。内核会在内部维护一棵**红黑树**，用于存储所有被监控的 Socket。

### 2.2 `epoll_ctl` (Control)

对红黑树进行操作：添加 (ADD)、修改 (MOD) 或删除 (DEL) 监控的 Socket。

- **监控事件**：通常包括 `EPOLLIN`（数据可读）、`EPOLLOUT`（数据可写）、`EPOLLET`（边缘触发）。

### 2.3 `epoll_wait`

主循环的核心。线程在此挂起，直到有 Socket 发生状态变化。

- **就绪列表**：内核会将就绪的事件拷贝到用户态定义的数组中，开发者只需遍历这个数组即可，无需像 `select` 那样扫描所有
  Socket，效率为 O(1)。

## 3. 触发模式：LT vs. ET

这是高性能编程中最关键的选择：

- **水平触发 (Level Triggered, LT)**：只要 Socket 缓冲区里有数据没读完，`epoll_wait` 就会一直提醒你。这是默认模式，较安全但系统调用频繁。
- **边缘触发 (Edge Triggered, ET)**：只有状态**发生变化**（如从无数据变为有数据）时才通知一次。
    - **要求**：必须配合 **非阻塞 (Non-blocking)** Socket 使用。
    - **实现逻辑**：收到通知后，必须循环 `read` 直到返回 `EAGAIN`（即缓冲区已读空），否则可能会丢失后续通知导致“死连接”。

## 4. Reactor (反应堆) 模式架构

Reactor 模式是基于事件驱动的软件设计模式，其核心架构如下：

### 4.1 Event Loop (事件循环)

程序的主干。它持有一个 `epoll_fd`，不断调用 `epoll_wait` 获取就绪事件。

### 4.2 Demultiplexer (多路分发器)

即 `epoll`。负责将产生的事件分发给对应的 Handler（处理器）。

### 4.3 核心 Handler 组件

1. **Acceptor**：负责处理监听 Socket（Listen FD）的 `EPOLLIN` 事件。当有新连接到来时，调用 `accept` 接受连接，并将新产生的
   `client_fd` 注册到 Epoll 中。
2. **Read Handler**：负责读取客户端发来的数据。将数据读入该连接专属的 `NetworkBuffer`，然后交给协议解析器（如 `RESPParser`）。
3. **Write Handler**：负责将 `NetworkBuffer` 中的响应数据发送给客户端。

## 5. 关键细节与最佳实践

### 5.1 非阻塞 Socket

在 Reactor 模式下，必须将 Socket 设置为 `O_NONBLOCK`。否则，如果 `read` 或 `write` 操作因为缓冲区满/空而阻塞，整个 Event
Loop（即整个服务）都会停摆。

### 2.2 粘包与半包处理

网络传输是字节流，不是完整的“包”。

- **半包**：一次 `read` 可能只读到了半条命令。此时 `NetworkBuffer` 缓存数据，等待下一次 `EPOLLIN` 事件。
- **粘包**：一次 `read` 可能读到了两条命令。此时解析器需要根据协议（如 RESP 的 `*` 或 `$` 长度字段）循环解析，直到 Buffer
  中不足一条完整指令。

### 5.3 错误处理 (EAGAIN / EINTR)

- **EAGAIN**：在非阻塞模式下，表示当前没有更多数据可读或缓冲区已满，应停止操作并等待下一次事件。
- **EINTR**：操作被信号中断，通常应立即重试。

## 6. 流程伪代码

```cpp
while (true) {
    int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    for (int i = 0; i < n; ++i) {
        if (events[i].data.fd == listen_fd) {
            // Acceptor: 处理新连接
            handle_accept(listen_fd);
        } else if (events[i].events & EPOLLIN) {
            // Read Handler: 读数据并解析
            handle_read(events[i].data.fd);
        } else if (events[i].events & EPOLLOUT) {
            // Write Handler: 发送剩余响应数据
            handle_write(events[i].data.fd);
        }
    }
}
```
