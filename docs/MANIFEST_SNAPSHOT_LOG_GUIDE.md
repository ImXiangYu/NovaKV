# Manifest 快照与增量日志机制复盘（Phase 2）

## 1. 当前机制（全量写）

NovaKV 当前的 Manifest 持久化是“全量覆盖”模式：

- 元数据发生变化后调用 `PersistManifestState()`
- `PersistManifestState()` 重写完整 `MANIFEST`
- 写入内容包含：
  - `next_file_number`
  - 全部 `sst_levels`
  - 全部 `live_wals`

这个模式的特点是状态文件始终完整、直观，但每次变更都会产生整文件写入。

## 2. 核心概念

### 全量写

一次小变更触发一次完整状态写入。

示例：

- `next_file_number` 只增加 1
- 仍会重写所有 SST/WAL 元数据

### I/O 放大

单次业务变更写入的数据量明显大于变更本身需要的数据量。

元数据规模增大后，这个现象会在高频路径（如 compaction 相关状态更新）中更明显。

## 3. 快照与增量日志的职责区别

- `MANIFEST`：某一时刻的完整状态快照
- `MANIFEST.log`：按时间顺序追加的增量变更记录（edit）

两者配合后的状态恢复语义：

1. 读取 `MANIFEST` 得到基础状态
2. 顺序回放 `MANIFEST.log` 增量
3. 得到当前最新 `manifest_state_`

## 4. 典型流程

### 运行期写入流程

1. 元数据发生变化
2. 生成对应 edit
3. 将 edit 追加到 `MANIFEST.log`
4. 内存中的 `manifest_state_` 与 edit 同步更新

### 启动恢复流程

1. 加载 `MANIFEST` 快照
2. 回放 `MANIFEST.log`
3. 生成最终恢复状态并继续 `LoadSSTables()/RecoverFromWals()`

### Checkpoint 流程

1. 将当前完整状态写入 `MANIFEST`
2. 截断或重建 `MANIFEST.log`
3. 新一轮增量记录从空日志开始

## 5. NovaKV 代码映射（现有语义）

当前元数据状态结构位于 `include/DBImpl.h`：

- `next_file_number`
- `sst_levels`
- `live_wals`

当前关键函数位于 `src/DBImpl.cpp`：

- `LoadManifestState()`：读取完整状态
- `PersistManifestState()`：写入完整状态
- `AllocateFileNumber()`：推进文件号
- `MinorCompaction()` / `CompactL0ToL1()`：变更 SST/WAL 集合
- `RecoverFromWals()`：恢复阶段补录与回放 WAL

## 6. 增量记录（Edit）语义示例

常见 edit 语义通常覆盖以下类型：

- `SetNextFileNumber`
- `AddSST(file_number, level)`
- `DelSST(file_number)`
- `AddWAL(wal_id)`
- `DelWAL(wal_id)`

每条 edit 表示一次原子元数据变化，按日志顺序应用可重建版本演进过程。

## 7. 复盘结论

从“全量 `MANIFEST` 重写”转向“`MANIFEST` 快照 + `MANIFEST.log` 增量”后，元数据持久化模型从“覆盖式状态存储”变为“状态 + 变更历史”双轨模型。  
这种模型同时保留了完整恢复基线（快照）和低成本持续更新（追加日志）。
