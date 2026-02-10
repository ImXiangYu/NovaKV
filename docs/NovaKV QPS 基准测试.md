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



