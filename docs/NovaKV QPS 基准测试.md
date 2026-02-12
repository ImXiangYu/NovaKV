# NovaKV QPS 基准测试


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

