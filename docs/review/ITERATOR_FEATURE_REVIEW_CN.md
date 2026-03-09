# 迭代器阶段功能复盘（中文）

## 结论
基于当前测试结果（`iterator_test` 全通过）与 TODO 勾选状态，`P1: 迭代器 / 范围扫描` 这一大功能可视为阶段性完成。

已覆盖能力：
- `Seek(start_key) / Next() / Valid() / key() / value()`
- 合并读取 `MemTable + L0 + L1`
- 同 key 新版本优先
- Tombstone（`kDeletion`）跨层遮蔽

## 本阶段你提出的关键疑问与结论
1. 为什么要迭代器，不是 `Get` 就够了吗？
- `Get` 只支持单点查询；范围扫描、全量遍历、后续导出/校验都需要迭代器。

2. `NewIterator()` 放哪里？为什么不放在 `DBIterator` 里？
- 建议放在 `DBImpl`，由 DB 负责组装数据源并创建迭代器对象（工厂职责）。

3. 是否需要独立类？
- 建议 `DBIterator` 独立（`DBImpl.h` 声明，`DBIterator.h/.cpp` 定义），便于解耦和后续扩展。

4. “可见记录”是什么意思？要不要直接返回 `ValueRecord`？
- 可见记录 = 裁决后的最终用户视图（最新版本胜出，若最新是 tombstone 则不可见）。
- 对外接口返回 `key/value` 即可；`ValueRecord.type` 用于内部裁决。

5. `rows_` 和 `pos_` 是什么？
- `rows_`：已裁决完成、按 key 升序的可见 KV 快照。
- `pos_`：当前游标位置下标（`size_t`），不是 key。

6. `Seek` 如何实现？
- 用 `lower_bound` 定位第一个 `>= start_key` 的 key；前提是 `rows_` 按 key 升序。

7. `Valid/key/value/Next` 的边界语义是什么？
- `Valid()`：`pos_ < rows_.size()`
- `Next()`：仅在 `Valid()==true` 时递增
- `key()/value()`：仅允许在 `Valid()==true` 时调用（建议断言）

8. 为什么 `ASSERT_TRUE` 在 `CollectFrom` 报编译错误？
- 因为该函数返回非 `void`，`ASSERT_*` 失败路径是 `return void`。改为分支判断 + `ADD_FAILURE`。

9. 遍历 MemTable 必须有 `end()` 吗？
- 你当前迭代器模型是 `Valid/Next` 风格，不需要 `end()`。

10. `rank/priority` 到底怎么定义？
- 关键是“越新越优先”。建议用 `priority`（值越大越新）防止语义写反。

11. `ForEach` 是否只回调 `kValue`？
- 不能只回调 `kValue`。必须把 `kDeletion` 也回调出来，才能做跨层 tombstone 遮蔽。

12. 为什么 `Iterator_HidesTombstoneAcrossLevels` 会失败？
- 典型原因是落盘时把类型硬编码成 `kValue`，导致 tombstone 丢失。应保留真实 `ValueType`。

## 本阶段实现策略（最终采用）
- 按“新到旧”顺序收集来源：`mem_ -> L0(新到旧) -> L1(新到旧)`
- 用 `seen.try_emplace(key, record)` 做“首见即最新”
- 最后仅输出 `kValue` 到 `rows_`，`kDeletion` 仅用于遮蔽

## 下一阶段建议
- 优先进入删除语义完整链路：
  - Tombstone 保留到最底层策略
  - 合并到最底层时清理 tombstone
- 然后补多 WAL 恢复与 compaction 统计

## QPS 备注（本阶段）
本阶段新增了迭代器/范围扫描功能，已实现 MemTable + L0 + L1 的合并可见性、同 key 新版本优先裁决，以及跨层 tombstone 遮蔽；本轮基准测试显示 QPS 无显著变化。
