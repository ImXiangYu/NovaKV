# fcntl 与非阻塞

## 1. 它在解决什么问题

在 Linux 里，socket 也是文件描述符（fd）。

这意味着，除了普通文件，socket 也可以被内核当成“一个可控制的 fd 对象”来看待。`fcntl` 就是 Linux 提供的通用控制接口，用来读取或修改一个 fd 的属性。

在 NovaKV 当前这一步里，我们使用 `fcntl` 的目的很明确：

- 给监听 socket 加上 `O_NONBLOCK`
- 让它适合放进后续的 `epoll` 事件循环里

## 2. `fcntl` 是什么

`fcntl` 可以理解成 `file control`。

它不是只做一件事的函数，而是一个“统一入口，多种控制命令”的系统调用。

函数原型可以理解为：

```cpp
int fcntl(int fd, int cmd, ...);
```

这 3 部分分别表示：

- `fd`：你要操作哪个文件描述符
- `cmd`：你想对这个 fd 做什么控制动作
- 第三个参数：某些命令需要的附加信息

## 3. 为什么它有这些参数

因为 `fcntl` 能做的事情很多，不只是设置 non-blocking。

比如常见命令有：

- `F_GETFL`：读取文件状态标志
- `F_SETFL`：设置文件状态标志
- `F_GETFD`：读取 fd descriptor 标志
- `F_SETFD`：设置 fd descriptor 标志

所以 `fcntl` 的设计是：

- 用 `fd` 指定操作对象
- 用 `cmd` 指定操作类型
- 用第三个参数补充这次操作需要的额外数据

这是一个“同一入口，按命令分派行为”的设计。

## 4. 我们当前为什么用它

当前阶段的目标是把 socket 改成 non-blocking。

原因是：

- 后面 NovaKV 要走 `epoll` 模型
- 一个线程会同时管理多个 fd
- 如果某个 fd 因为阻塞读写卡住，整个事件循环都会停摆

所以 socket 必须具备这样的行为：

- 当前做不了，就立刻返回
- 不要阻塞等待

这正是 `O_NONBLOCK` 的作用。

## 5. 标准写法为什么是两步

设置 non-blocking 的标准写法通常是：

```cpp
int flags = fcntl(fd, F_GETFL, 0);
if (flags < 0) {
  return false;
}

if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
  return false;
}
```

这两步都不能少。

### 5.1 第一步：`F_GETFL`

```cpp
int flags = fcntl(fd, F_GETFL, 0);
```

作用：

- 先把这个 fd 当前已有的状态标志读出来

为什么第三个参数写 `0`：

- 因为 `F_GETFL` 这个命令本身不需要额外参数
- 这里的 `0` 只是占位

成功时返回当前 flags，失败时返回 `-1`。

### 5.2 第二步：`F_SETFL`

```cpp
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

作用：

- 在原有状态的基础上，加上 `O_NONBLOCK`

为什么要写成 `flags | O_NONBLOCK`：

- `flags` 表示原本已有的状态
- `O_NONBLOCK` 表示新增的非阻塞标志
- 按位或的含义是“保留原状态，再额外挂上新状态”

## 6. 为什么不能直接写 `O_NONBLOCK`

错误示例：

```cpp
fcntl(fd, F_SETFL, O_NONBLOCK);
```

这类写法的问题是：

- 它没有保留 fd 原来已有的状态标志
- 你等于把旧状态直接覆盖掉了

所以规范做法一定是：

1. 先用 `F_GETFL` 读出原 flags
2. 再用 `flags | O_NONBLOCK` 写回去

## 7. 什么是 non-blocking

默认情况下，很多 socket 操作可能会阻塞：

- `accept` 没有新连接时可能阻塞
- `read` 没有数据时可能阻塞
- `write` 缓冲区满时可能阻塞

而 non-blocking 的含义是：

- 如果当前操作暂时做不了，不要等待
- 立刻返回给用户态

然后由上层代码根据错误码决定下一步动作，例如：

- `EAGAIN`
- `EWOULDBLOCK`

这正是 `epoll` / Reactor 模型能够成立的基础前提。

## 8. 为什么 NovaKV 后续一定需要它

NovaKV 后面的网络层是：

- `epoll`
- 单线程 IO 事件循环
- 一个线程管理多个连接

在这种模式下，只要有一个 fd 阻塞住：

- 整个 IO 线程就会卡住
- 其他连接也无法及时处理

所以 non-blocking 不是优化项，而是事件驱动网络模型里的基础要求。

## 9. `F_GETFL/F_SETFL` 和 `F_GETFD/F_SETFD` 的区别

这一点很容易混。

### 9.1 `F_GETFL / F_SETFL`

它们操作的是“文件状态标志（file status flags）”。

当前最常见的用途就是：

- 读取当前状态
- 加上 `O_NONBLOCK`

所以在网络编程里，把 socket 设成 non-blocking 时，一般用的就是这一组。

### 9.2 `F_GETFD / F_SETFD`

它们操作的是“文件描述符标志（file descriptor flags）”。

常见的是：

- `FD_CLOEXEC`

它和 `O_NONBLOCK` 不是一回事，也不是当前这一步的重点。

所以你现在只要先记住：

- 设 non-blocking：用 `F_GETFL / F_SETFL`
- 控制 `FD_CLOEXEC` 这类 descriptor 标志：用 `F_GETFD / F_SETFD`

## 10. 对应到 NovaKV 的函数

在当前 `TcpServer` 里，这一步对应的就是：

```cpp
bool TcpServer::SetNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    return false;
  }

  return true;
}
```

这个函数的职责非常单纯：

- 输入一个 fd
- 尝试给它加上 `O_NONBLOCK`
- 成功返回 `true`
- 失败返回 `false`

## 11. 一句话总结

`fcntl` 是 Linux 提供的 fd 控制接口。NovaKV 当前用它，是为了通过 `F_GETFL + F_SETFL` 给 socket 加上 `O_NONBLOCK`，让 socket 适配后续的 `epoll` 非阻塞事件循环。

## 12. 面试表达版本

如果面试时要简短回答，可以说：

“`fcntl` 是 Linux 下控制文件描述符属性的通用接口。因为 socket 也是 fd，所以可以用 `fcntl` 给 socket 设置 `O_NONBLOCK`。标准做法是先用 `F_GETFL` 读出原有 flags，再用 `F_SETFL` 写回 `flags | O_NONBLOCK`，这样可以保留原状态，只额外追加非阻塞标志。这是 `epoll` 事件驱动模型的基础要求。” 
