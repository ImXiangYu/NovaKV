# QPS 基准


无后台 compaction；仅 MemTable→SSTable 落盘；所有 SST 视作单层且不合并；读路径为 mem/imm + 全量 SST 扫描；日志关闭。

```
2026-02-10T20:40:18+08:00
Running /mnt/d/GithubProjects/NovaKV/cmake-build-debug/nova_bench
Run on (20 X 2688.01 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x10)
  L1 Instruction 32 KiB (x10)
  L2 Unified 1280 KiB (x10)
  L3 Unified 24576 KiB (x1)
Load Average: 0.08, 0.02, 0.01
***WARNING*** ASLR is enabled, the results may have unreproducible noise in them.
***WARNING*** Library was built as DEBUG. Timings may be affected.
---------------------------------------------------------------------
Benchmark           Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------
BenchPut       261835 ns        20951 ns        10000 items_per_second=47.7311k/s
BenchGet         1537 ns         1407 ns       522563 items_per_second=710.972k/s
```

启用自动 L0→L1 合并
```
2026-02-10T22:58:24+08:00
Running /mnt/d/GithubProjects/NovaKV/cmake-build-debug/nova_bench
Run on (20 X 2688.01 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x10)
  L1 Instruction 32 KiB (x10)
  L2 Unified 1280 KiB (x10)
  L3 Unified 24576 KiB (x1)
Load Average: 1.10, 0.60, 0.30
***WARNING*** ASLR is enabled, the results may have unreproducible noise in them.
***WARNING*** Library was built as DEBUG. Timings may be affected.
---------------------------------------------------------------------
Benchmark           Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------
BenchPut       187369 ns        16945 ns        38576 items_per_second=59.0151k/s
BenchGet         1509 ns         1379 ns       514553 items_per_second=724.957k/s
```

启动加载 SST + 落盘后删除旧 WAL

```
2026-02-11T12:46:49+08:00
Running /mnt/d/GithubProjects/NovaKV/cmake-build-debug/nova_bench
Run on (20 X 2688 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x10)
  L1 Instruction 32 KiB (x10)
  L2 Unified 1280 KiB (x10)
  L3 Unified 24576 KiB (x1)
Load Average: 0.14, 0.17, 0.18
***WARNING*** ASLR is enabled, the results may have unreproducible noise in them.
***WARNING*** Library was built as DEBUG. Timings may be affected.
---------------------------------------------------------------------
Benchmark           Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------
BenchPut       182854 ns        17704 ns        39695 items_per_second=56.4851k/s
BenchGet         1389 ns         1254 ns       581658 items_per_second=797.601k/s
```

新增 tombstone 持久化/读取/遍历遮蔽，SSTable 读路径对删除标记生效

```
2026-02-11T22:39:42+08:00
Running /mnt/d/GithubProjects/NovaKV/cmake-build-debug/nova_bench
Run on (20 X 2688 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x10)
  L1 Instruction 32 KiB (x10)
  L2 Unified 1280 KiB (x10)
  L3 Unified 24576 KiB (x1)
Load Average: 0.14, 0.26, 0.21
***WARNING*** ASLR is enabled, the results may have unreproducible noise in them.
***WARNING*** Library was built as DEBUG. Timings may be affected.
---------------------------------------------------------------------
Benchmark           Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------
BenchPut       193830 ns        18040 ns        35727 items_per_second=55.432k/s
BenchGet         1475 ns         1328 ns       498411 items_per_second=752.784k/s
```

将 WAL 恢复职责从 MemTable 构造迁移到 DBImpl 启动流程，统一采用 ValueRecord 接口（Put/Get 适配完成）；
重点观察启动后首轮写入与读取 QPS 变化，验证恢复路径重构未引入明显吞吐回退。

```
2026-02-12T14:23:06+08:00
Running /mnt/d/GithubProjects/NovaKV/cmake-build-debug/nova_bench
Run on (20 X 2688 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x10)
  L1 Instruction 32 KiB (x10)
  L2 Unified 1280 KiB (x10)
  L3 Unified 24576 KiB (x1)
Load Average: 1.12, 1.50, 0.90
***WARNING*** ASLR is enabled, the results may have unreproducible noise in them.
***WARNING*** Library was built as DEBUG. Timings may be affected.
---------------------------------------------------------------------
Benchmark           Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------
BenchPut       181807 ns        17705 ns        31092 items_per_second=56.4799k/s
BenchGet         1539 ns         1385 ns       525157 items_per_second=722.044k/s
```

新增 MANIFEST 持久化/恢复 next_file_number_（每次分配文件号后刷盘）；
重点观察写入路径 QPS 与 compaction 触发频率下的吞吐变化。

```
2026-02-12T15:11:45+08:00
Running /mnt/d/GithubProjects/NovaKV/cmake-build-debug/nova_bench
Run on (20 X 2688 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x10)
  L1 Instruction 32 KiB (x10)
  L2 Unified 1280 KiB (x10)
  L3 Unified 24576 KiB (x1)
Load Average: 0.30, 0.25, 0.20
***WARNING*** ASLR is enabled, the results may have unreproducible noise in them.
***WARNING*** Library was built as DEBUG. Timings may be affected.
---------------------------------------------------------------------
Benchmark           Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------
BenchPut       202727 ns        18085 ns        34919 items_per_second=55.2942k/s
BenchGet         1497 ns         1350 ns       503556 items_per_second=740.821k/s
```

本阶段新增了迭代器/范围扫描功能，已实现 MemTable + L0 + L1 的合并可见性、同 key 新版本优先裁决，以及跨层 tombstone 遮蔽；
本轮基准测试显示 QPS 无显著变化。

```
2026-02-12T17:49:18+08:00
Running /mnt/d/GithubProjects/NovaKV/cmake-build-debug/nova_bench
Run on (20 X 2688 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x10)
  L1 Instruction 32 KiB (x10)
  L2 Unified 1280 KiB (x10)
  L3 Unified 24576 KiB (x1)
Load Average: 0.53, 0.28, 0.21
***WARNING*** ASLR is enabled, the results may have unreproducible noise in them.
***WARNING*** Library was built as DEBUG. Timings may be affected.
---------------------------------------------------------------------
Benchmark           Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------
BenchPut       183782 ns        18661 ns        39637 items_per_second=53.588k/s
BenchGet         1488 ns         1342 ns       510886 items_per_second=745.205k/s
```

完成了删除语义闭环（WAL 透传、跨层 tombstone 遮蔽、最底层条件清理），语义正确性显著增强；预计 QPS 无结构性变化，波动主要来自 compaction 触发时机与环境噪声。
```
2026-02-18T23:38:03+08:00
Running /mnt/d/GithubProjects/NovaKV/cmake-build-debug/nova_bench
Run on (20 X 2688 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x10)
  L1 Instruction 32 KiB (x10)
  L2 Unified 1280 KiB (x10)
  L3 Unified 24576 KiB (x1)
Load Average: 0.76, 0.72, 0.35
***WARNING*** ASLR is enabled, the results may have unreproducible noise in them.
***WARNING*** Library was built as DEBUG. Timings may be affected.
---------------------------------------------------------------------
Benchmark           Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------
BenchPut       169363 ns        18284 ns        40288 items_per_second=54.693k/s
BenchGet         1237 ns         1226 ns       551235 items_per_second=815.88k/s
```

引入 Manifest 元数据恢复、追加式版本变更记录与多 WAL 恢复后，主要影响启动恢复路径与元数据维护路径；常态 Put/Get QPS 预期无结构性变化，波动主要来自 checkpoint 触发频率与 compaction 时机。

```
2026-02-21T18:59:28+08:00
Running /mnt/d/GithubProjects/NovaKV/cmake-build-debug/nova_bench
Run on (20 X 2688 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x10)
  L1 Instruction 32 KiB (x10)
  L2 Unified 1280 KiB (x10)
  L3 Unified 24576 KiB (x1)
Load Average: 0.18, 0.08, 0.07
***WARNING*** ASLR is enabled, the results may have unreproducible noise in them.
***WARNING*** Library was built as DEBUG. Timings may be affected.
---------------------------------------------------------------------
Benchmark           Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------
BenchPut       194892 ns        18869 ns        40472 items_per_second=52.9984k/s
BenchGet         1460 ns         1335 ns       499907 items_per_second=748.867k/s
```

存储核心路径（WAL/SST/MemTable）逻辑行为保持不变，但内部调用链已重组为组件化模式，且锁粒度从“全局入口锁”演进为“关键区段锁”；本次测试旨在验
证重构后的性能稳定性，预期单线程 Put/Get QPS 无结构性波动，并为后续引入后台 Flush 流程提供高纯净度的性能基线。

```text
2026-03-04T10:54:09+08:00
Running /mnt/d/GithubProjects/NovaKV/cmake-build-debug/nova_bench
Run on (20 X 2688 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x10)
  L1 Instruction 32 KiB (x10)
  L2 Unified 1280 KiB (x10)
  L3 Unified 24576 KiB (x1)
Load Average: 0.46, 0.45, 0.24
***WARNING*** ASLR is enabled, the results may have unreproducible noise in them.
***WARNING*** Library was built as DEBUG. Timings may be affected.
---------------------------------------------------------------------
Benchmark           Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------
BenchPut       251371 ns        20765 ns        10000 items_per_second=48.1579k/s
BenchGet         1284 ns         1236 ns       537892 items_per_second=809.154k/s
```

成功引入 “前台闪电切换 + 后台异步落盘” 架构。写路径（Put）现已实现 I/O 解耦：当 MemTable 满时，前台线程仅需原子级切换至
Immutable
MemTable 并触发后台任务即可返回，无需等待磁盘 I/O。后台线程采用 “三段锁” 策略执行 Minor Compaction，最大限度释放了 Get 与
Put
的并发空间。本次测试旨在验证异步化后的吞吐量上限与长尾延迟（P99）表现，预期写性能将有显著增幅，且 QPS 波动曲线将由于 I/O
停顿的消除而趋于平滑。
(测试数据太小未能体现出明显的提升)

```text
2026-03-04T12:33:47+08:00
Running /mnt/d/GithubProjects/NovaKV/cmake-build-debug/nova_bench
Run on (20 X 2688 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x10)
  L1 Instruction 32 KiB (x10)
  L2 Unified 1280 KiB (x10)
  L3 Unified 24576 KiB (x1)
Load Average: 1.24, 0.62, 0.37
***WARNING*** ASLR is enabled, the results may have unreproducible noise in them.
***WARNING*** Library was built as DEBUG. Timings may be affected.
---------------------------------------------------------------------
Benchmark           Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------
BenchPut       211981 ns        19392 ns        36046 items_per_second=51.5672k/s
BenchGet         1451 ns         1328 ns       509634 items_per_second=753.145k/s
```

------

完成 Phase 3 异步化架构升级，并将 MemTable 阈值提升至 10,000 条。本次调整旨在充分利用异步落盘带来的“前台写不受 I/O
阻塞”优势，通过增大内存缓冲区显著减少后台落盘频率及线程上下文切换开销。预期单线程写 QPS
将有数倍提升，且在多线程并发写入场景下将展现出更优的线性扩展能力与极低的长尾延迟。

```text
2026-03-04T12:39:31+08:00
Running /mnt/d/GithubProjects/NovaKV/cmake-build-debug/nova_bench
Run on (20 X 2688 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x10)
  L1 Instruction 32 KiB (x10)
  L2 Unified 1280 KiB (x10)
  L3 Unified 24576 KiB (x1)
Load Average: 1.48, 0.70, 0.45
***WARNING*** ASLR is enabled, the results may have unreproducible noise in them.
***WARNING*** Library was built as DEBUG. Timings may be affected.
---------------------------------------------------------------------
Benchmark           Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------
BenchPut       171050 ns        17252 ns        41250 items_per_second=57.9641k/s
BenchGet          643 ns          593 ns      1188940 items_per_second=1.68546M/s
```
