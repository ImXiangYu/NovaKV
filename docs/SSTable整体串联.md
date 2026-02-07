## 一、 宏观架构：SSTable 到底是什么？

SSTable（Sorted String Table）是 LSM-Tree 存储引擎中**不可变（Immutable）**的磁盘文件。

- **有序性**：内部所有 KV 对严格按 Key 升序排列。
- **分块性**：为了避免一次性加载大文件，它被切分为多个 Data Block。
- **自包含性**：文件末尾自带“地图”（Index Block）和“魔数校验”（Footer）。

------

## 二、 物理格式：文件里的每一位都在干什么？

一个完整的 SSTable 文件在磁盘上的布局如下：

| **组成部分**     | **详细内容**                               | **作用**                                             |
| ---------------- | ------------------------------------------ | ---------------------------------------------------- |
| **Data Block 1** | `[KLen][K][VLen][V]...`                    | 存放实际的 KV 数据，通常为 4KB。                     |
| **Data Block N** | 同上                                       | 最后一个数据块可能不满 4KB。                         |
| **Index Block**  | `[KLen][LastK][VLen][Offset+Size]`         | **地图**。记录每个 Data Block 的最大 Key 和位置。    |
| **Footer**       | `[IndexOffset(8)][IndexSize(8)][Magic(8)]` | **罗盘**。固定 24 字节，定位索引块并校验文件合法性。 |

------

## 三、 写入链路：`SSTableBuilder` 的精密操作

当你调用 `builder.Add(key, value)` 到 `Finish()` 时，内部发生了以下精密逻辑：

### 1. 缓冲与切分 (Data Block 积木)

- `Builder` 内部维护一个 `BlockBuilder`。
- 当你持续 `Add` 时，它会检查当前缓存大小是否超过了 `kBlockSize` (4KB)。
- **触发阈值**：一旦超过，立刻调用 `Flush()`。
  - 将当前块的所有 KV 序列化为二进制流写向磁盘。
  - **核心动作**：记录下这个块的 **Last Key**（当前 Add 的这个 key）以及它在文件中的 **Offset** 和 **Size**。
  - 将这些元数据存入 `index_entries_` 列表。

### 2. 封底与索引 (The Grand Finale)

当你调用 `Finish()` 时：

- **收尾 Data Block**：如果有残留数据，刷入磁盘。
- **构建 Index Block**：遍历 `index_entries_`，利用 `BlockBuilder` 把刚才收集的所有“地图碎片”打包成一个大的索引块写进文件。
- **写入 Footer**：这是最后一步。它记下索引块在文件中的位置，并在最后放上 **Magic Number**。

------

## 四、 读取链路：`SSTableReader` 的高效寻踪

读取不是简单地 `read()`，而是利用 **OS 的虚拟内存特性** 玩的一场“瞬移”。

### 1. 握手与映射 (`Open`)

- **`fstat`**：先拿到文件大小。
- **`mmap`**：建立“传送门”。这一步最关键，它并没有真的把文件读进内存，而是把文件映射到了进程的**虚拟地址空间**。
- **`ReadFooter`**：跳到文件最后 24 字节。验证魔数，拿到索引块的“家庭住址”。

### 2. 构建大脑 (`ReadIndexBlock`)

- 根据 Footer 提供的偏移量，去 `mmap` 的内存里解析索引。
- 由于索引块也是 `BlockBuilder` 格式，我们按照 `[KLen][K][VLen][V]` 的规矩读出每一个 `IndexEntry`。
- **结果**：内存里有了一个有序的 `std::vector<IndexEntry>`，这就是我们的导航仪。

### 3. 精准取货 (`Get`)

这是你刚才通过测试的核心：

- **第一跳：二分查找**。在 `index_entries_` 中用 `std::lower_bound` 寻找。
  - 策略：找第一个 `LastKey >= target_key` 的块。
- **第二跳：指针切片**。找到对应的 `Offset` 和 `Size` 后，直接通过 `data_ + Offset` 定位到那个 4KB 的 Data Block 内存区域。
- **第三跳：线性扫描**。在 4KB 内部顺序解析 KV，直到找到目标或扫描结束。

------

## 五、 重点避坑总结

1. **内存对齐与序列化**：为什么不能直接强转指针？因为磁盘数据是 Packed（紧凑）的，而 C++ 结构体有内存对齐（Padding）。**必须用 `memcpy` 或逐字节解析。**
2. **mmap 的优势**：减少了内核态到用户态的内存拷贝（Zero-copy 思想）。
3. **原子性保护**：`Open` 失败时必须确保 `fd` 被关闭，`mmap` 被释放。利用 `SSTableReader` 的析构函数和初始值（`-1` 和 `MAP_FAILED`）实现**异常安全的资源管理**。
4. **格式一致性**：索引块（Index Block）本质上也是一个数据块（Data Block），必须遵循同样的 `Length-Prefixed`（长度前缀）编码规则。