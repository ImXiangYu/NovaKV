## 🛠️ 第三阶段：从内存到磁盘的跃迁

在 LSM-Tree 架构中，数据流转遵循 **MemTable (Active) -> MemTable (Immutable) -> SSTable (L0/L1...)** 的路径。

### 1. 冻结与转换：Immutable MemTable

当当前的 SkipList 达到预设阈值（如 4MB 或 16MB）时，我们不能直接阻塞用户写入。

- **双缓冲机制**：将当前的 SkipList 标记为 `Immutable`（只读），并立即创建一个新的 `Active` SkipList 接收新写入。
- **后台落盘**：触发一个后台线程，将 `Immutable` MemTable 中的有序数据“拍扁”写入磁盘，生成第一个 SSTable 文件。

### 2. 存储格式：SSTable (Sorted String Table)

SSTable 是 LSM-Tree 的基石。它不仅仅是一个文件，而是一个**分块存储、带索引**的结构。你需要在磁盘上复刻类似如下的布局：

| **组成部分**     | **作用**                                                     |
| ---------------- | ------------------------------------------------------------ |
| **Data Blocks**  | 连续存储的 Key-Value 对，通常每 4KB 一个 Block，方便按页读取。 |
| **Meta Index**   | 记录每个 Data Block 的起始位置和最小/最大 Key。              |
| **Bloom Filter** | **（本阶段重点）** 存储该文件所有 Key 的特征，快速判定“目标 Key 是否不在该文件”。 |
| **Footer**       | 位于文件末尾，记录 Meta Index 的偏移量，是读取文件的“入口”。 |

### 3. 读取路径：从内存到磁盘的“漏斗”

查询一个 Key 的优先级顺序如下：

1. **MemTable (Active)**：最新数据。
2. **MemTable (Immutable)**：等待落盘的数据。
3. **L0 SSTables**：最近落盘的文件（注意：L0 层文件之间 Key 可能重叠）。
4. **L1+ SSTables**：经过 Compaction 的文件（各文件间 Key 严格有序，无重叠）。

------

## 💡 核心挑战：读放大与写放大

作为 408 选手，面试官一定会问你：**“为什么不直接用 B+ 树？”**

你需要通过代码实现来证明你对以下痛点的理解：

- **写放大 (Write Amplification)**：Compaction 频繁搬运数据导致 IO 浪费。
- **读放大 (Read Amplification)**：为了找一个 Key，可能要查好几个 SSTable。
  - **应对方案**：我们要实现的 **Bloom Filter** 就在这里起作用。它能通过 $O(1)$ 的位运算跳过 90% 以上不包含该 Key 的文件。