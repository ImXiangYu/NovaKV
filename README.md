# NovaKV
基于 LSM-Tree 的 KV 存储系统学习实践。  
项目定位：面向 C++ 后端求职的工程化项目（非生产级），强调“可实现、可验证、可讲解”。

## 项目目标
- 通过从零实现核心存储链路，理解 LSM-Tree 关键机制。
- 形成可面试讲解的双主线能力：
  - 存储主线：WAL / MemTable / SSTable / Compaction / Recovery
  - 网络主线：Client-Server 协议 / 并发模型 / 服务稳定性（规划中）

## 当前状态（现状）
### 已实现
- MemTable（跳表）+ WAL 写前日志。
- WAL CRC32 校验与启动回放。
- SSTable Builder/Reader（Data Block / Index / Footer / Bloom Filter）。
- DBImpl 统一读写路径，支持 L0/L1。
- Minor Compaction（MemTable -> SST）与基础 L0->L1 合并。
- 启动恢复：加载 `.sst`、回放 `.wal`。
- `MANIFEST` 持久化并恢复 `next_file_number_`。
- 迭代器扫描：合并 MemTable + L0 + L1，遵循“新版本优先”，并隐藏 tombstone。

### 进行中
- 删除语义完整链路（跨层 tombstone 保留/清理策略）。
- 完整版本元数据（Manifest 记录存活文件与层级关系）。
- 后台任务（imm flush / major compaction）与并发策略收敛。

### 规划中
- 网络服务化（`epoll + 线程池`）与 client-server 协议。

详细顺序请见：`docs/TODO.md`（顺序执行版）。

## 构建与运行
本项目使用 CMake + C++17。测试与基准依赖 GoogleTest/Google Benchmark（通过 FetchContent 拉取，需要网络）。

### 构建
```bash
cmake -S . -B build
cmake --build build
```

### 运行全部测试
```bash
ctest --test-dir build --output-on-failure
```

### 运行单个测试（示例）
```bash
./build/dbimpl_test
./build/iterator_test
```

### 运行基准测试
```bash
./build/nova_bench
```

说明：
- `nova_test` 目前是 `main.cpp` 的手动示例程序（非完整 DB 客户端）。
- `SSTableReader` 使用 `mmap` 等 POSIX 接口，建议在 Linux/WSL 环境下开发和运行。

## 目录结构
```text
include/     头文件（MemTable/SkipList/WAL/SSTable/DBImpl 等）
src/         核心实现
tests/       单元测试（每个 *.cpp 会生成独立测试目标）
benchmark/   基准测试
docs/        学习笔记、阶段总结、路线文档
```

## 文档与路线
- 路线与优先级：`docs/TODO.md`
- 协作上下文：`docs/AGENT.md`
- 学习笔记：`docs/` 目录下各专题文档

## 文档策略
- 只在关键节点输出文档：设计分叉点、关键问题复盘、阶段完成总结。
- 文档目标是复盘与面试表达，不追求记录每个实现细节。

## 边界说明
- 这是个人学习项目，优先保证结构清晰与可验证正确性。
- 当前不追求生产级能力（完整多层 compaction、分布式能力、工业级容错等）。
