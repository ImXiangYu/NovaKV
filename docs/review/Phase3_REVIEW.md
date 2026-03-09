# NovaKV Phase 3 技术复盘：异步化与并发深度解析

## 1. 核心架构演进：从“同步阻塞”到“异步流水线”

### 【广度描述】

实现了后台线程处理 Minor Compaction，前台 `Put` 线程在 MemTable 满时只需切换指针即可返回，不再等待磁盘 I/O。

### 【深度对质（面试热点）】

* **问：你的异步化是怎么做到的？**
    * **答**：采用了 **“双缓冲（Double Buffering）”** 思想。引入 `imm_` (Immutable MemTable)。当 `mem_`
      达到阈值（10,000条），前台持有锁进行“闪电切换”：将 `mem_` 赋给 `imm_`，立即创建新 `mem_` 和新 WAL，随后唤醒后台线程。
* **问：如果后台落盘（Flush）跟不上前台写入速度怎么办？**
    * **答**：实现了 **Write Stall（写停顿）** 机制。在 `Put` 路径中使用 `std::condition_variable_any::wait`。如果发现 `imm_`
      依然非空（说明上一轮落盘还没完），前台线程会主动阻塞，直到后台清理完 `imm_` 并发出通知。这保护了系统不会因为内存暴涨而崩溃。

---

## 2. 致命 Bug 复盘：非递归锁产生的资源死锁

### 【事件回顾】

在实现 `MinorCompaction` 自动触发 `L0->L1` 压缩时，程序抛出了 `std::system_error: Resource deadlock avoided`。

### 【深度解析（体现解决问题的能力）】

* **根本原因**：`std::shared_mutex` 是**非递归锁**。
    * 后台线程在 `MinorCompaction` 函数内部已经持有了 `state_mu_` 的 `unique_lock`。
    * 随后又在同一个线程里调用了 `CompactL0ToL1`。
    * `CompactL0ToL1` 的第一行又试图去获取 `state_mu_` 的 `unique_lock`。
    * 结果：同一线程自我竞争，触发死锁保护机制。
* **解决方案**：采用 **“锁外触发（Lock-free Trigger）”**。在 `MinorCompaction` 的锁区间内仅计算布尔标志位 `trigger_l0_to_l1`
  ，在 `unlock` 之后再根据标志位安全调用压缩函数。

---

## 3. 并发控制模型：一写多读

### 【核心选型】

* **写锁 (`write_mu_`)**：`std::mutex`。保证全局只有一个 `Put` 在进行，简化了 WAL 顺序写入和 MemTable 并发的复杂性。
* **状态锁 (`state_mu_`)**：`std::shared_mutex`。
    * **读操作 (`Get`)**：使用 `shared_lock`。允许多个线程同时读取 SST 和 MemTable，极大提升了 QPS。
    * **元数据变更 (`Switch/Install`)**：使用 `unique_lock`。确保在切换指针或更新文件列表时，没有任何读操作在进行。

---

## 4. 可观测性：从“盲飞”到“仪表盘”

### 【为什么要加 GetStatus？】

* **深度思考**：异步系统最大的问题是“不可预测”。通过引入 `std::atomic` 计数器和 `chrono` 计时，我们量化了 **落盘耗时（Flush
  Latency）**。
* **面试亮点**：我不仅写了功能，还通过 `GetStatus` 暴露了系统内部指标。这使得我能准确观察到：当阈值从 1000 调优到 10000
  时，QPS 提升了数倍，同时 Minor Compaction 的频率大幅下降。

---

## 5. 待优化的深度方向（面试的杀手锏）

如果面试官问：“你这个项目还有什么遗憾？”你可以抛出这些深度思考：

1. **Read Amplification（读放大）**：目前 `Get` 需要依次查找 Mem -> Imm -> L0 -> L1。随着文件增多，性能会下降。**对策**：引入
   Bloom Filter。
2. **Write Amplification（写放大）**：频繁的 Compaction 会导致一份数据被多次重写到磁盘。**对策**：优化 Compaction 触发策略（如
   Size-tiered）。
3. **锁细粒度化**：目前 `state_mu_` 锁住了整个层级列表。**对策**：引入 Copy-on-Write (COW) 或版本控制（MVCC）思想，让读操作完全不需要锁。
