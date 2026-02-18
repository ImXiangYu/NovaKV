# Phase_1复盘

## 1. 目标与范围

本轮 Phase 1 的目标是打通删除语义闭环：`DEL` 在内存、WAL、SST、重启恢复、跨层读取中都保持一致且可验证。  
本轮不追求性能优化，优先解决“语义正确性”。

---

## 2. 本轮解决的关键问题（不止旧值复活）

## 2.1 问题A：WAL 删除语义丢失

### 现象
- 删除通过 `Put(kDeletion)` 表达，但 WAL 写入路径曾默认写 `kValue`。
- 重启回放后删除语义可能失真。

### 根因
- `MemTable` 写 WAL 时没有透传 `ValueRecord.type/value`。

### 修复
- `MemTable::Put` 统一写 `wal_.AddLog(key, value.value, value.type)`。

### 价值
- 删除语义在 WAL 层可恢复，重启一致性基础成立。

---

## 2.2 问题B：MemTable 删除语义不一致（物理删除 vs tombstone）

### 现象
- 删除如果物理删节点，会丢掉“删除版本”本身。

### 根因
- 物理删除与 LSM 多版本裁决模型冲突。

### 修复
- `Remove` 改为写 tombstone（`ValueRecord{kDeletion, ""}`），不做物理删除。

### 价值
- 删除信息可跨层传播，后续 compaction 才能正确裁决。

---

## 2.3 问题C：数据模型分裂（泛型 MemTable 与 DB 内核语义错位）

### 现象
- `MemTable<K,V>` 与 DB 内核实际只需要 `string + ValueRecord`。
- 语义入口多样，排查困难。

### 根因
- 早期泛型设计服务练习需求，与当前数据库内核目标不一致。

### 修复
- `MemTable` 内核化：收敛为 `std::string -> ValueRecord`。

### 价值
- 语义入口统一，后续恢复、compaction、网络映射都更稳定。

---

## 2.4 问题D：潜在不安全序列化路径（`ValueRecord` + `memcpy`）

### 现象
- 泛型序列化路径对非平凡类型存在风险。

### 根因
- 早期通用 `Serialize/Deserialize` 设计不适合 `ValueRecord` 这种包含 `std::string` 的结构。

### 修复
- 内核化后移除该泛型路径，统一走明确字段编码（`type + value`）。

### 价值
- 降低未定义行为风险，提升可维护性。

---

## 2.5 问题E（核心）：跨层读取“旧值复活”

### 现象
- 新层 key 为 tombstone 时，`GET` 仍可能返回旧层 value。

### 根因
- 旧 `SSTableReader::Get` 只有两态：  
  `true=命中值`，`false=未命中或命中 tombstone`。  
- `DBImpl` 无法区分 tombstone 与 miss，继续查旧层导致复活。

### 修复
- 新增内部接口 `GetRecord(key, ValueRecord*)`，提供三态信息。
- `DBImpl::Get` 改为：命中 tombstone 立即短路返回未命中。
- 修复实现细节：判断应使用当前命中记录 `rec.type`，而非入参 `value.type`。

### 价值
- 删除语义在跨层读取中稳定成立，杜绝“旧值复活”。

---

## 2.6 问题F：测试与接口脱节

### 现象
- `memtable_test` 仍使用旧泛型接口，无法覆盖新语义。

### 修复
- 测试迁移到 `MemTable + ValueRecord`。
- 增加/保留关键用例：tombstone 写入、手动回放、重启后语义验证。

### 价值
- 测试与实现同构，回归可信。

---

## 2.7 问题G：`const` 语义链不完整（读接口可维护性问题）

### 现象
- `MemTable::Get const` 受阻，IDE 报 `SkipList::search_element` 缺少 `const`。

### 根因
- 读路径函数 const 传播链不完整。

### 修复
- 将 `SkipList::search_element` 调整为 `const`。
- 配套让 `MemTable::Get` 具备 `const` 语义。

### 价值
- 只读路径边界更清晰，减少后续并发/接口误用。

---

## 2.8 问题H（收尾）：tombstone 清理时机不完整

### 现象
- 虽然 tombstone 已能跨层遮蔽旧值，但 compaction 阶段尚未实现“最底层条件清理”。
- 长期保留无必要 tombstone 会增加空间与读取遍历负担。

### 根因
- `L0 -> L1` 合并时，对 `kDeletion` 采用无条件写出策略。

### 修复
- 新增 `HasVisibleValueInL1(key)` 判定逻辑：仅当旧 `L1` 仍有可见旧值时保留 tombstone。
- `CompactL0ToL1()` 改为 tombstone 条件写出：需要遮蔽才写，不需要则丢弃。
- 增加专项测试：`CompactDropsBottomMostTombstonesWithoutCreatingNewSST`，验证可清理 tombstone 不会生成新的 L1 SST。

### 价值
- 完成 Phase 1 最后一环：删除语义从“逻辑正确”推进到“语义正确 + 空间行为合理”。
- 为后续多层 compaction 的 tombstone 生命周期策略提供可复用判定范式。

---

## 3. 如果这些问题不修，会出现什么连锁后果

- `DEL` 后 `GET` 结果不稳定，甚至重启后变化。
- 跨层 compaction 验证无意义，因为基础裁决已错。
- 网络层协议映射会继承错误语义（`GET/SCAN` 可靠性下降）。
- 测试出现“偶发失败”与“难复现问题”，开发效率持续下降。

---

## 4. 本轮产出与当前状态

已完成：
- MemTable 内核化
- WAL 透传 `ValueRecord.type/value`
- 内存层 tombstone 语义统一
- `GetRecord` 三态读取 + `DBImpl` 短路裁决
- `L0 -> L1` tombstone 条件清理（仅在需要遮蔽旧值时保留）
- 相关测试通过并同步到 TODO

未完成（Phase 1 剩余）：
- 无（Phase 1 存储语义闭环已完成）

---

## 5. 本轮对后续开发的正向影响

- 为 Phase 2 恢复与元数据闭环提供稳定语义前提。
- 为后续多层 compaction（L1->L2...）提供 tombstone 生命周期判定模板。
- 为网络层语义映射提供确定行为（减少协议层补丁逻辑）。

---

## 6. 可复用的“大问题复盘模板”

后续可以按这个模板复盘：

1. 现象：测试/线上表现是什么。  
2. 影响面：影响哪些语义、模块、用户行为。  
3. 根因：设计问题、实现问题、还是边界条件问题。  
4. 修复：改了哪些接口、哪些调用点、哪些测试。  
5. 验证：最小复现是否消失，回归测试是否覆盖。  
6. 代价与收益：复杂度变化、未来收益。  
7. 遗留项：本轮故意不做、下一轮做什么。

---

## 7. 一句话结论

本轮不仅修了“旧值复活”，还完成了最底层条件清理 tombstone，使删除语义在入口、编码、读取、恢复、压实清理与测试上形成完整闭环。
