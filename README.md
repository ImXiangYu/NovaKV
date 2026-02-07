# NovaKV
基于 LSM-Tree 的 KV 存储系统学习实践，聚焦核心数据结构与落盘流程的可读实现。（自我学习项目，非生产级）

## 项目目标
- 理解 LSM-Tree 核心组件的职责与协作方式
- 用 C++17 从零实现可运行的最小 KV 存储链路
- 通过单元测试和阶段性文档沉淀学习过程

## 当前实现
- MemTable: 基于跳表的内存写入层
- WAL: 写前日志，保证崩溃恢复的可重放性
- SSTable: Builder/Reader/BlockBuilder 的落盘与读取
- BloomFilter: 查询加速的布隆过滤器
- DBImpl: 对外入口与核心流程串联

## 快速开始

### 构建
本项目使用 CMake + C++17，并通过 FetchContent 拉取 GoogleTest（需要网络）。

```bash
cmake -S . -B build
cmake --build build
```

### 运行示例

```bash
./build/nova_test
```

### 运行测试

```bash
cd build
ctest
```

也可以直接运行单个测试可执行文件，例如 `block_test`、`dbimpl_test`。

## 目录结构

```
include/    头文件（MemTable/SkipList/SSTable/WAL 等）
src/        核心实现
tests/      单元测试
docs/       学习笔记与阶段总结
```

## 学习笔记
项目过程中记录了关键模块的实现思路与验证过程，详见 `docs/`：
- `docs/SSTable 与 LSM-Tree 存储层.md`
- `docs/WriteAheadLog.md`
- `docs/SkipList跳表.md`
- `docs/测试结果.md`

## 路线与边界
- 这是个人学习项目，重点在结构清晰与可验证
- 不追求完整的线上特性（如压缩、分层压实、并发控制）
- 欢迎基于学习目的交流或提 Issue 指正
