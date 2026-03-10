# TcpServer 骨架总览

## 1. 这个骨架在解决什么问题

这份骨架的核心目标是 3 件事：

1. 把监听 socket 建起来
2. 把 `epoll` 主循环骨架建起来
3. 把连接上下文的创建和销毁入口建起来

也就是说，这里做的不是：

- 读 RESP
- 执行 `SET/GET/DEL/RSCAN`
- 回写响应

而是先把这些能力未来要运行的“框架”搭好。

## 2. 核心对象

### 2.1 `Connection`

`Connection` 表示“一个客户端连接的上下文”。

它现在负责保存每个连接自己的状态：

- `fd`
- `input_buffer`
- `output_buffer`
- `RESPParser`
- `closing`

它当前只是状态容器，不承载业务逻辑。

### 2.2 `TcpServer`

`TcpServer` 表示“整个服务端本体”。

它负责管理：

- `listen_fd_`
- `epoll_fd_`
- 所有在线连接 `connections_`
- 事件循环
- 连接接入
- 连接关闭

它和 `Connection` 的关系是：

- `TcpServer` 管所有连接
- `Connection` 只表示某一个连接的状态

## 3. 核心函数

### 3.1 启动与停止相关

- `TcpServer::TcpServer(DBImpl* db)`
- `TcpServer::~TcpServer()`
- `TcpServer::Start(uint16_t port)`
- `TcpServer::Stop()`

这组函数负责服务端生命周期。

### 3.2 监听 socket 初始化相关

- `TcpServer::InitListenSocket(uint16_t port)`
- `TcpServer::SetNonBlocking(int fd)`

这组函数负责把监听 fd 建起来，并切成 non-blocking。

### 3.3 epoll 管理相关

- `TcpServer::InitEpoll()`
- `TcpServer::AddEpollEvent(int fd, uint32_t events)`
- `TcpServer::UpdateEpollEvent(int fd, uint32_t events)`
- `TcpServer::RemoveEpollEvent(int fd)`

这组函数负责把 fd 挂到 `epoll` 上，以及后续的修改和删除。

### 3.4 事件分发与连接生命周期相关

- `TcpServer::Run()`
- `TcpServer::HandleAccept()`
- `TcpServer::HandleConnectionEvent(int fd, uint32_t events)`
- `TcpServer::CloseConnection(int fd)`

这组函数负责：

- 等待事件
- 接入新连接
- 处理连接事件
- 关闭连接

## 4. 这些函数之间是什么关系

如果从“谁先被调用”来看，当前骨架的主调用链是：

```text
Start()
  -> InitListenSocket()
       -> SetNonBlocking(listen_fd_)
  -> InitEpoll()
  -> AddEpollEvent(listen_fd_, EPOLLIN)

Run()
  -> epoll_wait(...)
  -> if fd == listen_fd_
       -> HandleAccept()
            -> accept(...)
            -> SetNonBlocking(client_fd)
            -> connections_.try_emplace(...)
            -> AddEpollEvent(client_fd, EPOLLIN | EPOLLRDHUP)
  -> else
       -> HandleConnectionEvent(fd, events)
            -> if ERR/HUP/RDHUP
                 -> CloseConnection(fd)
                      -> RemoveEpollEvent(fd)
                      -> close(fd)
                      -> connections_.erase(fd)

Stop()
  -> close all client fd
  -> clear connections_
  -> close epoll_fd_
  -> close listen_fd_
```

这条链路就是整个服务端骨架的主调用链。

## 5. 每个函数在这个骨架里负责什么

### 5.1 `Start`

职责：

- 编排启动流程
- 串起监听 socket 初始化、`epoll` 初始化和监听 fd 注册

它自己不直接写底层系统调用细节，而是调下面几个子函数。

可以把它理解成“服务端启动总控”。

### 5.2 `InitListenSocket`

职责：

- 创建 TCP 监听 socket
- 配置 `SO_REUSEADDR`
- 设成 non-blocking
- `bind` 到指定端口
- `listen`

它的目标是得到一个可用于监听新连接的 `listen_fd_`。

### 5.3 `SetNonBlocking`

职责：

- 给任意 fd 加上 `O_NONBLOCK`

它既服务于：

- `listen_fd_`
- 后续 `accept` 出来的 `client_fd`

### 5.4 `InitEpoll`

职责：

- 创建一个 `epoll` 实例

它解决的问题是：

- 让服务端后续有一个统一的事件分发器

### 5.5 `AddEpollEvent / UpdateEpollEvent / RemoveEpollEvent`

职责：

- 集中封装 `epoll_ctl`

这组函数的意义是：

- 避免以后把 `epoll_ctl` 散落在各个业务函数里
- 后续如果要切换 `EPOLLOUT`、修改事件关注，就直接复用这些接口

### 5.6 `Run`

职责：

- 进入 `epoll_wait` 主循环
- 取出本轮就绪事件
- 根据 fd 类型分发给不同 handler

它是整个 Reactor 的“主干”。

### 5.7 `HandleAccept`

职责：

- 从 `listen_fd_` 接出新的客户端连接
- 为每个新连接建立 `Connection`
- 注册进 `epoll`

它处理的是“新连接到来”，不是“读客户端命令”。

### 5.8 `HandleConnectionEvent`

职责：

- 处理某个 `client_fd` 上发生的事件

这里先完成了最小关闭分支：

- `EPOLLERR`
- `EPOLLHUP`
- `EPOLLRDHUP`

遇到这些情况时，会走 `CloseConnection(fd)`。

`EPOLLIN` 和 `EPOLLOUT` 现在还是空位，留给后面的读写链路。

### 5.9 `CloseConnection`

职责：

- 统一关闭某个在线连接

关闭顺序是：

1. 从 `epoll` 中删除
2. `close(fd)`
3. 从 `connections_` 中删除

它是连接生命周期的统一销毁入口。

### 5.10 `Stop`

职责：

- 停掉 server
- 关闭所有 client fd
- 清空连接表
- 关闭 `epoll_fd_`
- 关闭 `listen_fd_`

它保证服务端整体退出时有一条统一的清理路径。

## 6. 当前骨架的事件流是怎样的

如果从“运行时事件”来看，当前逻辑可以这样理解：

### 阶段 1：启动

当调用 `Start(port)` 时：

1. 创建 `listen_fd_`
2. 把它绑定到端口
3. 设成 non-blocking
4. 创建 `epoll_fd_`
5. 把 `listen_fd_` 注册到 `epoll`

此时服务端已经具备“监听新连接”的能力。

### 阶段 2：事件循环

当调用 `Run()` 时：

1. 进入 `epoll_wait`
2. 阻塞等待一批就绪事件
3. 遍历这些事件

然后分两类：

- 如果事件来自 `listen_fd_`
  - 说明有新连接到来
  - 走 `HandleAccept()`

- 如果事件来自某个 `client_fd`
  - 说明这个客户端连接上有事件
  - 走 `HandleConnectionEvent(fd, events)`

### 阶段 3：接入新连接

在 `HandleAccept()` 中：

1. 循环 `accept`
2. 每拿到一个 `client_fd`
3. 设成 non-blocking
4. 创建 `Connection`
5. 插入 `connections_`
6. 注册到 `epoll`

此时这个客户端正式成为在线连接，后续它的读写事件都会走 `HandleConnectionEvent()`。

### 阶段 4：关闭连接

在 `HandleConnectionEvent()` 中，如果发现：

- `EPOLLERR`
- `EPOLLHUP`
- `EPOLLRDHUP`

就说明这个连接已经异常或断开。

于是走：

```text
HandleConnectionEvent()
  -> CloseConnection(fd)
       -> RemoveEpollEvent(fd)
       -> close(fd)
       -> erase from connections_
```

这样连接生命周期才闭环。

## 7. 为什么先做骨架再接主链路

因为 NovaKV 的网络主链路分层是有顺序的：

1. 先把 server 骨架和连接生命周期搭好
2. 再接 `read -> input_buffer`
3. 再接 `RESPParser`
4. 再接 `CommandExecutor`
5. 最后接 `output_buffer -> write`

如果一开始就直接写 `read -> parse -> execute -> send`，很容易把：

- socket 生命周期
- 连接管理
- 协议解析
- DB 调用
- 回写逻辑

全部搅在一起。

这种做法的本质，是先立住模块边界。

## 8. 尚未覆盖的链路

虽然骨架已经搭起来了，但还没有完成下面这些环节：

- `EPOLLIN` 的读链路
- `NetworkBuffer` 写入输入缓冲
- `RESPParser` 解析命令
- `CommandExecutor` 执行命令
- 响应写入 `output_buffer`
- `EPOLLOUT` 发送回包

也就是说，这个 server 已经能：

- 启动
- 监听
- 接入新连接
- 管理连接生命周期

但还不能真正处理客户端命令。

## 9. 这层骨架的工程价值

这层骨架工作的价值不在于“功能看起来多”，而在于先把网络层的基础结构搭对了。

它带来的收益是：

- 监听 fd 和 client fd 的职责已经分开
- `epoll` 管理接口已经收口
- `Connection` 上下文已经明确挂点
- 连接创建和销毁入口已经形成闭环
- 明天接单线程读链路时，有明确的落点可接

## 10. 一句话总结

这层骨架完成的是 NovaKV 网络服务的第一层基础结构：把 `TcpServer`、`Connection`、监听 socket、`epoll` 主循环、新连接接入和连接关闭这几个最基础的模块立住，为后续的 `read -> parse -> execute -> write` 单线程请求链路打地基。

## 11. 面试表达版本

如果面试时要简短总结这一层工作，可以这样说：

“我先没有急着把命令执行链路直接塞进网络层，而是先把服务端骨架搭起来。具体包括：初始化监听 socket，创建 epoll 实例，把监听 fd 注册进去；事件循环里把监听事件和连接事件分开处理；新连接到来时建立独立的 `Connection` 上下文，并注册到 epoll；连接断开时走统一的 `CloseConnection` 清理路径。这样后面再接 RESP 解析和 DB 执行时，网络层的生命周期和模块边界已经是稳定的。” 
