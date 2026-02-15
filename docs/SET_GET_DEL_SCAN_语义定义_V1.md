# NovaKV 对外语义定义 V1（SET/GET/DEL/SCAN）

> 目标：统一“调用方看到的行为契约”，避免内部实现细节泄漏到上层。  
> 范围：当前存储内核（`DBImpl` + `DBIterator`），网络协议层可直接映射本语义。

## 1. 当前接口与命令映射

当前存储接口（代码）：

```cpp
void Put(const std::string& key, ValueRecord& value);
bool Get(const std::string& key, ValueRecord& value) const;
std::unique_ptr<DBIterator> NewIterator();
```

命令语义映射（对外）：

- `SET key value` -> `Put(key, ValueRecord{ValueType::kValue, value})`
- `DEL key` -> `Put(key, ValueRecord{ValueType::kDeletion, ""})`
- `GET key` -> `Get(key, record)` 后按语义判定是否命中
- `SCAN start_key` -> `NewIterator()->Seek(start_key)` 后连续 `Next()`

## 2. 统一语义（V1 约定）

### 2.1 SET

- 输入：`key`、`value`（字符串）。
- 语义：同 key 多次写入，**最新版本生效**（last-write-wins）。
- 持久化：先 WAL 后内存。
- 建议对外返回：`OK` / `INVALID_ARGUMENT` / `INTERNAL_ERROR`。

### 2.2 GET

- 输入：`key`。
- 语义：
  - 若不存在该 key，返回未命中。
  - 若该 key 最新版本为 tombstone（删除标记），也返回未命中。
  - 否则返回最新 value。
- 查找优先级：`MemTable -> ImmMemTable -> L0(新到旧) -> L1(新到旧)`。
- 建议对外返回：`OK(value)` / `NOT_FOUND` / `INTERNAL_ERROR`。

### 2.3 DEL

- 输入：`key`。
- 语义：写入 tombstone，不做立即物理删除（由 compaction 处理）。
- 幂等：重复删除同一 key，结果等价于一次删除。
- 建议对外返回：`OK`（当前实现更适合该语义；不区分是否原本存在）。

### 2.4 SCAN

- 输入：`start_key`。
- 语义：
  - 返回所有 `key >= start_key` 的可见 KV。
  - 结果按 key 升序。
  - 若同 key 多版本，仅返回最新可见版本。
  - tombstone 对用户不可见。
- 当前实现边界：暂无 `end_key` / `limit` 参数，调用方需自行截断。

## 3. 参数与边界约束（当前阶段）

- 存储层当前未统一做严格参数校验（例如空 key）。
- 建议网络层 V1 增加参数校验并映射为 `INVALID_ARGUMENT`，但不得改变本语义的可见结果。

## 4. 重启一致性语义

- 重启后应满足：
  - `SET` 生效数据可恢复；
  - `DEL` 语义仍成立（`GET` 仍未命中）；
  - `SCAN` 仍隐藏 tombstone，仅输出可见值。

## 5. 当前代码与 V1 语义的差异（你可按需改代码）

以下是当前实现中需要你手动确认/修正的点（我本次未改代码）：

1. `include/MemTable.h` 的 `Put` 目前写 WAL 时固定使用 `ValueType::kValue`。  
   影响：当 `DBImpl` 用 `Put(kDeletion)` 表达删除时，WAL 回放可能丢失删除语义。

2. `src/DBImpl.cpp` 的 `Get` 在命中 `MemTable/imm_` tombstone 时，当前会返回 `true` 并把 `record.type = kDeletion` 交给调用方判断。  
   建议：在 `DBImpl::Get` 内部统一将 tombstone 视为未命中，减少调用方分支。

## 6. 验收建议（对应 TODO Phase 0/1）

- `GET` 对 tombstone 返回未命中（内存层、落盘层、跨层一致）。
- 删除后重启，`GET` 仍未命中。
- `SCAN` 不返回 tombstone key。
- 文档与接口一致（本文件 + 头文件签名 + 测试断言）。
