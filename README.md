# NovaKV

一个基于 LSM-Tree 的 KV 存储系统学习项目。  
这份 README 只负责提供项目全局视角：项目是什么、包含什么、如何使用、从哪里进入。

## 项目是什么

NovaKV 是一个面向 C++ 后端学习与项目表达的 KV 数据库实现，核心目标有两条：

- 存储主线：理解并实现 WAL、MemTable、SSTable、Compaction、Recovery
- 网络主线：基于 RESP 协议将存储能力服务化，补齐连接管理、并发模型与回包链路

它的定位是“可实现、可验证、可讲解”的工程化项目，而不是生产级数据库。

## 项目包含什么

### 存储层

- MemTable（跳表）
- WAL 写前日志
- SSTable Builder / Reader
- LSM-Tree 读写路径
- Minor Compaction 与基础层间合并
- Manifest 元数据管理
- 启动恢复与多 WAL 回放
- 迭代器与范围扫描

### 网络层

- RESP Parser
- RESP Encoder
- 命令分发层 `CommandExecutor`
- 面向后续 `epoll + 线程池` 的网络主链路设计

### 测试与基准

- `tests/`：单元测试
- `benchmark/`：基准测试

## 当前对外能力

当前对外命令语义包括：

- `SET key value`
- `GET key`
- `DEL key`
- `RSCAN start_key`

说明：

- `GET` 命中 tombstone 时按未命中处理
- `RSCAN` 返回所有 `key >= start_key` 的可见 KV
- `RSCAN` 是 NovaKV 自定义范围扫描命令，不是 Redis 原生 `SCAN`
- 项目使用 RESP 协议，因此可以用 `redis-cli` 作为客户端联调工具

语义定义见：[对外语义V1](/home/ayu/GithubProjects/NovaKV/docs/spec/%E5%AF%B9%E5%A4%96%E8%AF%AD%E4%B9%89V1.md)

## 如何使用这个项目

### 构建

```bash
cmake -S . -B build
cmake --build build
```

### 运行全部测试

```bash
ctest --test-dir build --output-on-failure
```

### 运行单个测试

```bash
./build/dbimpl_test
./build/iterator_test
./build/command_executor_test
```

### 运行基准

```bash
./build/nova_bench
```

### 手动入口

```bash
./build/nova_test
```

说明：

- 项目使用 CMake + C++17
- 测试和基准依赖 GoogleTest / Google Benchmark
- `SSTableReader` 使用 `mmap`，建议在 Linux / WSL 环境运行

## 从哪里进入项目

### 代码入口

- 存储核心：`include/DBImpl.h`、`src/DBImpl.cpp`
- 网络协议：`include/network/`、`src/network/`
- 命令分发：`include/network/CommandExecutor.h`、`src/network/CommandExecutor.cpp`
- 测试：`tests/`
- 基准：`benchmark/db_bench.cpp`

### 文档入口

- 总任务清单：[docs/TODO.md](/home/ayu/GithubProjects/NovaKV/docs/TODO.md)
- 协作约束：[docs/AGENT.md](/home/ayu/GithubProjects/NovaKV/docs/AGENT.md)
- 规格文档：[docs/spec/](/home/ayu/GithubProjects/NovaKV/docs/spec)
- 设计文档：[docs/design/](/home/ayu/GithubProjects/NovaKV/docs/design)
- 实现指南：[docs/guide/](/home/ayu/GithubProjects/NovaKV/docs/guide)
- 复盘总结：[docs/review/](/home/ayu/GithubProjects/NovaKV/docs/review)
- 研发日志：[docs/log/](/home/ayu/GithubProjects/NovaKV/docs/log)

## 目录结构

```text
include/     头文件
src/         核心实现
tests/       单元测试
benchmark/   基准测试
docs/        规格、设计、指南、复盘、日志
```
