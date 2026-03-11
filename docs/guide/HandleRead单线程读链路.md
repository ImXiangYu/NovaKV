# HandleRead 单线程读链路

## 1. `HandleRead` 在解决什么问题

`epoll` 返回 `EPOLLIN` 只说明一件事：

- 某个客户端 fd 现在有数据可读

但它不会替服务端完成下面这些工作：

- 把数据从内核 socket 缓冲区搬到用户态
- 处理半包、粘包和一次到达多条命令
- 把字节流解析成 RESP 命令
- 执行命令并生成响应
- 把响应暂存起来等待后续发送

`HandleRead` 的职责就是把这条链路串起来：

```text
socket -> input_buffer -> RESPParser -> CommandExecutor -> output_buffer
```

## 2. 调用位置

`HandleRead` 属于客户端读事件处理函数。

它通常由这条链路触发：

```text
Run()
  -> epoll_wait(...)
  -> HandleConnectionEvent(fd, events)
  -> if events contains EPOLLIN
       -> HandleRead(fd)
```

`HandleRead` 处理的是：

- 已经建立好的 client fd
- 这个连接上的读事件

它不处理：

- 新连接接入
- 监听 fd
- 直接 socket 回写

## 3. 核心目标

`HandleRead` 的最小目标是：

1. 找到这个 fd 对应的 `Connection`
2. 把 socket 数据读入 `Connection::input_buffer`
3. 尽可能解析出完整 RESP 命令
4. 调用 `CommandExecutor`
5. 把响应写入 `Connection::output_buffer`
6. 如果有待发送数据，开启 `EPOLLOUT`

## 4. 推荐处理流程

### 4.1 先找到连接对象

第一步不是直接读 socket，而是先从 `connections_` 里找到 `Connection`。

原因是：

- `fd` 只是一个整数
- 真正的连接状态都保存在 `Connection` 里
- 包括：
  - `input_buffer`
  - `output_buffer`
  - `parser`

如果找不到这个连接，直接返回比继续操作更安全。

### 4.2 把 socket 数据读进 `input_buffer`

这一步通常通过：

- `conn.input_buffer.ReadFromFd(fd, &savedErrno)`

完成。

原因是：

- `EPOLLIN` 只表示“现在可以读”
- 数据真正进入协议层之前，必须先从内核搬到用户态缓冲区

这里不能直接读到局部字符串里，因为 RESP 需要长期保留未解析完的数据：

- 半包需要等待下一次拼接
- 粘包需要连续拆多条命令
- 多条命令可能一次到达

所以输入缓冲必须是连接级状态，而不是函数局部变量。

### 4.3 按 `ReadFromFd` 返回值分流

#### `n > 0`

含义：

- 这次确实读到了新字节

下一步应该进入 parser 循环。

#### `n == 0`

含义：

- 对端正常关闭连接

这时应关闭连接：

- `CloseConnection(fd)`

#### `n < 0`

这时要看 `savedErrno`。

如果是：

- `EAGAIN`
- `EWOULDBLOCK`

说明非阻塞 socket 当前已经没有更多数据可读，这不是错误，只是本轮读取结束。

如果是：

- `EINTR`

说明系统调用被信号打断，当前阶段可以直接返回，等待下一次事件或下一轮处理。

如果是其他错误，则通常直接关闭连接。

## 5. 为什么 parser 必须放在循环里

读到正数字节后，不能只 parse 一次。

因为一次读事件可能对应多种情况：

- 只读到半条命令
- 恰好一条完整命令
- 一次读到多条完整命令
- 一条完整命令后面跟着半条下一条命令

所以正确模式是：

- 只要 `input_buffer` 里还能继续解析出完整命令，就继续解析

这能保证：

- 已经到达的完整命令不会被拖到下一次事件再处理
- 粘包场景能一次性消费掉
- 剩余半包仍然留在 `input_buffer` 中等待补齐

## 6. parser 的三种返回状态如何处理

### 6.1 `ParseStatus::SUCCESS`

表示：

- 当前 `input_buffer` 里成功解析出一条完整命令

这时应立刻：

- 调用 `executor_.Execute(command, &conn.output_buffer)`

然后继续下一轮解析。

这样做的原因是：

- 单线程阶段的最短路径就是“解析完立刻执行”
- 当前已经有现成的 `CommandExecutor` 和 `output_buffer`，不需要再加中间层

### 6.2 `ParseStatus::INCOMPLETE`

表示：

- 当前剩余数据还不够组成一条完整命令

这时应该停止解析并返回，等待下次 `EPOLLIN`。

不能做的事：

- 不能清空 `input_buffer`
- 不能关闭连接

因为这正是半包场景的正常状态。

### 6.3 `ParseStatus::ERROR`

表示：

- 协议非法，当前连接发送了错误 RESP

最小闭环阶段的最简单策略是：

- 直接关闭连接

后续如果需要更像 Redis，再演进成：

- 先写协议错误响应
- 再标记关闭

## 7. 为什么响应先写 `output_buffer`，而不是直接 `write`

`HandleRead` 的职责是“产出响应”，不是“发送响应”。

把执行结果先写到 `Connection::output_buffer` 有几个好处：

- 读路径和写路径职责分开
- 后续更容易接入 `EPOLLOUT`
- 一次产生多条响应时可以统一缓存
- 线程池阶段也能沿用同样的回写边界

因此单线程阶段的合理分工是：

- `HandleRead` 负责读、解析、执行、生成响应
- `HandleWrite` 负责真正把响应写到 socket

## 8. 为什么有响应后要开启 `EPOLLOUT`

当 `CommandExecutor` 往 `output_buffer` 写入数据后，这些字节还没有真正发到客户端。

所以如果：

- `conn.output_buffer.ReadableBytes() > 0`

就应该修改这个 fd 的 `epoll` 关注事件，加入 `EPOLLOUT`。

这样后续在 fd 可写时：

- `HandleWrite` 才会被唤醒
- 响应才会真正发送出去

## 9. 一个容易踩的点

不要在 `HandleConnectionEvent()` 里仅仅因为看到了 `EPOLLRDHUP`，就抢先关闭连接。

原因是：

- 对端半关闭时，内核可能同时返回 `EPOLLIN | EPOLLRDHUP`
- 这意味着“还有最后一批数据可读，但对端之后不会再写了”

如果此时先关闭连接，最后一包数据可能会被直接丢掉。

更稳的做法是：

- 先处理 `EPOLLIN`
- 在真正 `read()` 返回 `0` 时，再关闭连接

## 10. 一句话总结

`HandleRead` 的本质就是：

- 把 socket 中到达的数据读进连接输入缓冲
- 尽可能解析出完整 RESP 命令
- 逐条执行并把响应写进输出缓冲
- 为后续 `EPOLLOUT` 发送阶段做好准备
