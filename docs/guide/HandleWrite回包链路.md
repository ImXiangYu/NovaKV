# HandleWrite 回包链路

## 1. `HandleWrite` 在解决什么问题

`HandleRead` 完成的是：

- 读取请求
- 解析 RESP
- 执行命令
- 把响应写入 `output_buffer`

但这还不等于响应已经发回客户端。

响应真正写到 socket，需要另一个阶段来完成：

```text
output_buffer -> socket
```

`HandleWrite` 的职责就是：

- 在 fd 可写时，把连接输出缓冲中的数据尽可能写到 socket
- 写完后关闭无效的 `EPOLLOUT` 关注

## 2. 调用位置

`HandleWrite` 由客户端写事件触发。

典型调用链：

```text
HandleRead(fd)
  -> CommandExecutor
  -> response 写入 conn.output_buffer
  -> UpdateEpollEvent(fd, EPOLLIN | EPOLLRDHUP | EPOLLOUT)

Run()
  -> epoll_wait(...)
  -> HandleConnectionEvent(fd, events)
  -> if events contains EPOLLOUT
       -> HandleWrite(fd)
```

它处理的是：

- 已经建立好的 client fd
- 已经有待发送响应的连接

## 3. 核心目标

`HandleWrite` 的最小目标是：

1. 找到这个 fd 对应的 `Connection`
2. 从 `output_buffer` 中取出待发送字节
3. 尽量写到 socket
4. 未写完则保留剩余数据，等待下一次 `EPOLLOUT`
5. 全部写完后关闭 `EPOLLOUT`

## 4. 推荐处理流程

### 4.1 先找到连接对象

第一步仍然是从 `connections_` 中查找 `fd` 对应的 `Connection`。

原因是：

- `fd` 只是索引
- 真正待发送的数据在 `Connection::output_buffer`

而且这里不能假设连接一定还在。

可能出现这种情况：

- 同一轮事件里先执行 `HandleRead`
- `HandleRead` 因协议错误或 EOF 已关闭连接
- 后面这轮事件又继续走到 `HandleWrite`

所以 `HandleWrite` 必须自己重新 `find` 一次。

### 4.2 为什么要循环写

可写事件到来时，不能只尝试写一次就结束。

因为一次 `write` 可能只发生以下几种情况：

- 写完全部数据
- 只写出一部分
- 当前暂时写不进去

所以合理模式是：

- 只要 `output_buffer` 还有可读字节，就继续尝试写

这样可以让当前可发送的数据尽量在这一轮事件里被发掉，避免无意义地拖到下一次事件。

### 4.3 写入的数据来源

写 socket 时，数据来源应该是：

- `conn.output_buffer.Peek()`
- `conn.output_buffer.ReadableBytes()`

语义是：

- 从输出缓冲当前还未发送的起始位置开始
- 尽量发送剩余所有待发送字节

## 5. `write` 返回值如何处理

### 5.1 `n > 0`

表示：

- 成功写出了一部分或全部数据

这时应当：

- `conn.output_buffer.Retrieve(n)`

原因是：

- `write` 不保证一次写完全部内容
- 只能把已经真正发出去的前 `n` 个字节从缓冲中消费掉
- 剩余部分仍要保留，供后续继续发送

### 5.2 `n < 0` 且 `errno == EINTR`

表示：

- 系统调用被信号打断

这不是 socket 真正出错，可以继续重试。

### 5.3 `n < 0` 且 `errno == EAGAIN || errno == EWOULDBLOCK`

表示：

- 当前 non-blocking socket 暂时写不进去

这不是错误，而是当前这一轮发送到此为止。

这时应当：

- 退出写循环
- 保留 `output_buffer` 中的剩余数据
- 保持 `EPOLLOUT` 继续开启

等下一次可写事件再继续发送。

### 5.4 其他错误

表示：

- 真实写错误

这时最简单安全的策略是：

- 直接关闭连接

## 6. 为什么写空后要关闭 `EPOLLOUT`

如果 `output_buffer` 已经写空，说明当前连接没有待发送数据。

这时应把事件关注改回：

- `EPOLLIN | EPOLLRDHUP`

而不是继续保留 `EPOLLOUT`。

原因是：

- 大多数 socket 在大部分时间里都会被认为“可写”
- 如果长期监听 `EPOLLOUT`，即使没有任何数据要发，`epoll_wait` 也会反复被唤醒
- 这会制造大量无意义事件

所以 `EPOLLOUT` 的正确使用方式是：

- 有数据要发时才打开
- 发空后立即关闭

## 7. 为什么要检查 `UpdateEpollEvent` 的返回值

`UpdateEpollEvent` 内部依赖 `epoll_ctl(EPOLL_CTL_MOD)`。

如果修改失败，但调用方忽略返回值，就可能出现：

- 用户态以为已经开启或关闭了 `EPOLLOUT`
- 实际内核中的 epoll 关注状态并没有同步成功

这会导致连接状态和 epoll 状态失配。

当前阶段最简单的处理策略是：

- `UpdateEpollEvent` 失败，就关闭连接

## 8. `closing` 标志在这里的作用

`Connection` 中的 `closing` 表示：

- 这个连接准备关闭，但可能还有待发送响应

这类连接不应该立刻关闭，而应该：

1. 继续通过 `HandleWrite` 把剩余响应发完
2. 等 `output_buffer` 写空后再真正关闭

所以 `HandleWrite` 是最适合消费 `closing` 标志的位置。

## 9. 一句话总结

`HandleWrite` 的本质就是：

- 在 fd 可写时，尽量把输出缓冲中的响应发到 socket
- 写不完就保留剩余部分等待下一次 `EPOLLOUT`
- 写完就关闭 `EPOLLOUT`，避免无意义唤醒
