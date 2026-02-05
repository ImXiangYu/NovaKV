## 🛠️ 第三阶段：从内存到磁盘的跃迁

在 LSM-Tree 架构中，数据流转遵循 **MemTable (Active) -> MemTable (Immutable) -> SSTable (L0/L1...)** 的路径。

### 冻结与转换：Immutable MemTable

当当前的 SkipList 达到预设阈值（如 4MB 或 16MB）时，我们不能直接阻塞用户写入。

- **双缓冲机制**：将当前的 SkipList 标记为 `Immutable`（只读），并立即创建一个新的 `Active` SkipList 接收新写入。
- **后台落盘**：触发一个后台线程，将 `Immutable` MemTable 中的有序数据“拍扁”写入磁盘，生成第一个 SSTable 文件。

### 存储格式：SSTable (Sorted String Table)

SSTable 是 LSM-Tree 的基石。它不仅仅是一个文件，而是一个**分块存储、带索引**的结构。你需要在磁盘上复刻类似如下的布局：

| **组成部分**     | **作用**                                                     |
| ---------------- | ------------------------------------------------------------ |
| **Data Blocks**  | 连续存储的 Key-Value 对，通常每 4KB 一个 Block，方便按页读取。 |
| **Meta Index**   | 记录每个 Data Block 的起始位置和最小/最大 Key。              |
| **Bloom Filter** | **（本阶段重点）** 存储该文件所有 Key 的特征，快速判定“目标 Key 是否不在该文件”。 |
| **Footer**       | 位于文件末尾，记录 Meta Index 的偏移量，是读取文件的“入口”。 |

### 读取路径：从内存到磁盘的“漏斗”

查询一个 Key 的优先级顺序如下：

1. **MemTable (Active)**：最新数据。
2. **MemTable (Immutable)**：等待落盘的数据。
3. **L0 SSTables**：最近落盘的文件（注意：L0 层文件之间 Key 可能重叠）。
4. **L1+ SSTables**：经过 Compaction 的文件（各文件间 Key 严格有序，无重叠）。

------

### 核心挑战：读放大与写放大

作为 408 选手，面试官一定会问你：**“为什么不直接用 B+ 树？”**

你需要通过代码实现来证明你对以下痛点的理解：

- **写放大 (Write Amplification)**：Compaction 频繁搬运数据导致 IO 浪费。
- **读放大 (Read Amplification)**：为了找一个 Key，可能要查好几个 SSTable。
  - **应对方案**：我们要实现的 **Bloom Filter** 就在这里起作用。它能通过 $O(1)$ 的位运算跳过 90% 以上不包含该 Key 的文件。

## SSTable 内部逻辑

一个工业级的 SSTable 文件（如 LevelDB 格式）逻辑上分为以下几个部分。我建议你按**从底层到高层**的顺序开发：

### 第一阶段：Data Block Builder（数据块构建）

- **逻辑**：磁盘 IO 是按页（通常 4KB）读写的。我们不能一条一条写 KV，而是要把它们打包成一个个 **Block**。
- **开发任务**：写一个 `BlockBuilder` 类。它负责把 KV 对存入缓冲区，直到大小接近 4KB，然后返回这个“包”。
- **408 考点**：文件系统的块管理、缓冲区设计。

### 第二阶段：SSTable Index（索引记录）

- **逻辑**：为了不用遍历整个文件就能找到 Key，我们需要记录每个 Block 的**起始偏移量**和**该块内最大的 Key**。
- **开发任务**：在写完所有 Data Block 后，紧接着写一个 Index Block。

### 第三阶段：Footer（文件尾/句柄）

- **逻辑**：SSTable 是从后往前读的。文件最后几个字节是固定长度的 **Footer**，里面存着 Index Block 的位置。
- **开发任务**：实现 Footer 的序列化，确保程序打开 `.sst` 文件时，先读末尾，拿到“目录”地址。

### 第四阶段：Bloom Filter（性能核武器）

- **逻辑**：如果文件很多，一个个查索引也慢。布隆过滤器能通过位运算快速告诉你：“这个 Key 绝对不在这个文件里”。
- **开发任务**：实现一个简单的哈希位图结构。