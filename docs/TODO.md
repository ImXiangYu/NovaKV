# NovaKV TODO（功能完整路径）

目标：实现类似 LevelDB 的功能集合（以正确性为先，性能为次）。

## P0 - 正确性与恢复
- [ ] Manifest / 版本元数据
  - [x] 持久化并加载 `next_file_number_`
  - [ ] 记录存活文件与层级信息
  - [ ] Manifest 追加式更新，保证崩溃可恢复
- [ ] 多 WAL 恢复
  - [x] 启动时扫描所有 `.wal`
  - [x] 按文件号顺序回放
  - [ ] 成功落盘后删除旧 WAL

## P1 - 核心读功能
- [x] 迭代器 / 范围扫描
  - [x] MemTable 迭代器
  - [x] SSTable 迭代器
  - [x] 多层合并迭代器（新版本优先）
- [ ] 删除语义完整链路
  - [ ] Tombstone 保留策略（保留到最底层）
  - [ ] 合并到最底层时清理 Tombstone

## P2 - Compaction 与层级
- [ ] 多层 compaction（L1 -> L2 -> ...）
  - [ ] 每层简单 size 阈值
  - [ ] 基础 overlap 选择策略
- [ ] Compaction 统计（输入/输出字节、耗时）

## P3 - 并发与后台任务
- [ ] 读写锁策略（至少写串行 + 多读并发）
- [ ] imm_ 后台 flush（minor compaction）
- [ ] 后台 compaction 线程（major compaction）

## P4 - 写入接口与批处理
- [ ] WriteBatch（多 put/delete 原子性）
- [ ] WAL 可选 fsync（可配置耐久性）

## P5 - 诊断与测试
- [ ] 删除语义测试（落盘 + 重启 + 跨层遮蔽）
- [x] 迭代器正确性测试
- [ ] 多 WAL 恢复测试
- [ ] QPS 记录持续维护（每新增功能标注对比）

## 可选拓展
- [ ] 后台 compaction 限速
- [ ] 前缀 Bloom filter 或每块 filter
- [ ] Manifest 轮转 / 压缩
