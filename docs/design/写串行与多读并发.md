# Phase 3：写串行 + 多读并发边界文档（V1）

## 1. 目标

本阶段并发目标只有一个：  
在不改变 `SET/GET/DEL/RSCAN` 语义的前提下，实现 **写串行** 与 **多读并发**，并避免死锁与读到失效结构。

## 2. 方案

- 写：加写锁，同一时刻只允许一个写线程进入。
- 读：加读锁，允许多个读线程并发。

但还需要补一层：  
除了 `MemTable` 内部读写锁，还必须有 **DB 级共享状态锁** 来保护 `mem_/imm_/levels_` 的生命周期和切换过程。

## 3. 锁模型与职责

建议采用三层锁，职责严格分离：

1. `write_mu_`（`DBImpl`，`std::mutex`）

- 作用：串行化 `Put` 写路径。
- 约束：同一时刻仅一个写线程执行前台写。

2. `state_mu_`（`DBImpl`，`std::shared_mutex`）

- 作用：保护 DB 全局共享状态。
- 保护对象：
    - `mem_` / `imm_` 指针切换
    - `levels_` 结构变更
    - `active_wal_id_`
    - compaction/flush 的安装阶段
- 使用方式：
    - 读路径（`Get` / `NewIterator`）持有共享锁
    - 状态切换/安装持有独占锁

3. `MemTable::rw_lock_`（已有，`std::shared_mutex`）

- 作用：保护单个 memtable 的 SkipList 与 WAL 写入顺序。
- `MemTable::Put` 用独占锁，`MemTable::Get` 用共享锁。

## 4. 操作边界

### 4.1 `Put`

- 必须先拿 `write_mu_`（写串行入口）。
- 常规写入：直接写当前 `mem_`。
- 当 `mem_` 达阈值：
    - 仅在“`mem_ -> imm_` 切换 + 新 mem/WAL 发布”这个短区间持有 `state_mu_` 独占锁。
    - 长耗时刷盘不应持有 `state_mu_` 独占锁。

### 4.2 `Get`

- 持有 `state_mu_` 共享锁，保证读路径期间 `mem_/imm_/levels_` 不被并发改结构。
- 读顺序固定：`mem_ -> imm_ -> L0(新到旧) -> L1(新到旧)`。
- 多个 `Get` 可以并发执行。

### 4.3 `NewIterator`

- 持有 `state_mu_` 共享锁构建一致性视图。
- 合并顺序与 `Get` 一致，保持“新版本优先、tombstone 对用户不可见”。

### 4.4 `Compact/Flush`

- 文件读写与 merge 计算阶段：不持有 `state_mu_` 独占锁。
- 仅在“安装新 SST + 更新 levels_ + 清理旧 reader/WAL”短区间持有 `state_mu_` 独占锁。

## 5. 并发关系矩阵（V1）

- `Put` vs `Put`：不并发（写串行）。
- `Get` vs `Get`：并发允许。
- `Get` vs `NewIterator`：并发允许（同为共享锁）。
- `Put` vs `Get`：允许并发，但在 `MemTable` 局部临界区会短暂互斥。
- `Compact/Flush 安装阶段` vs `Get/NewIterator`：互斥（结构变更保护）。

## 6. 锁顺序（全局不变量）

统一顺序：

1. `write_mu_`（若需要）
2. `state_mu_`（共享或独占）
3. `MemTable::rw_lock_`（由 memtable 内部获取）

禁止事项：

- 禁止持有 `MemTable::rw_lock_` 后再请求 `state_mu_`。
- 禁止在同一作用域内把 `state_mu_` 从共享锁升级为独占锁（必须释放后重拿）。
- 禁止在持有 `state_mu_` 独占锁时执行长时间磁盘 IO。

## 7. Phase 3 非目标（避免范围膨胀）

- 本轮不引入多后台线程调度系统。
- 本轮不做 major compaction 后台化（仍可手动/同步触发）。
- 本轮不改外部命令语义与文件格式（WAL/SST/Manifest）。

## 8. 验收口径（文档层）

当以下三点都能明确回答时，边界文档完成：

1. 任意两个 API 并发时，是否允许并发、由哪把锁保证？
2. 哪些代码区段必须短锁，哪些区段绝不能持有大锁？
3. 是否存在违反锁顺序导致死锁的路径？
