# NovaKV TODO（顺序执行版）

目标：从上到下逐步完成，最终形成“数据库内核 + 网络服务”双主线面试项目。  
执行原则：不跳阶段；每完成一个 Phase，再进入下一个。

## Phase 0 - 基线与规则（先把地基固定）
- [x] 冻结当前基线并记录现状
  - [x] 记录当前可通过的测试集
  - [x] 记录当前基线 QPS（Put/Get）
- [x] 明确统一对外语义
  - [x] `GET` 对 tombstone 返回未命中
  - [x] 明确 `SET/GET/DEL/SCAN` 语义定义文档

## Phase 1 - 存储语义闭环（最优先）
- [x] 删除语义完整链路
  - [x] `MemTable` 内核化（类型收敛到 `ValueRecord`）
  - [x] `Put` 透传 `ValueRecord.type/value` 到 WAL（不再固定 `kValue`）
  - [x] 收敛泛型序列化路径，避免 `ValueRecord` 走 `memcpy` 序列化
  - [x] 内存层删除与查询语义统一
  - [x] 跨层读取时 tombstone 遮蔽旧值
  - [x] 仅在最底层满足条件时清理 tombstone
- [x] 对应测试补齐
  - [x] `MemTable` 内核化回归测试（tombstone 写入/回放/重启）
  - [x] 删除语义测试（内存、落盘、跨层）
  - [x] 重启后删除语义测试
- [x] 迭代器 / 范围扫描已具备
  - [x] MemTable 迭代器
  - [x] SSTable 遍历
  - [x] 多层合并（新版本优先）
  - [x] 对用户隐藏 tombstone

## Phase 2 - 恢复与元数据闭环
- [x] Manifest 版本元数据完善
  - [x] 持久化并加载 `next_file_number_`
  - [x] 记录存活文件与层级映射
  - [x] 采用追加式版本变更记录
  - [x] 启动时可完整恢复版本状态
- [x] 多 WAL 恢复闭环
  - [x] 启动扫描所有 `.wal`
  - [x] 按文件号顺序回放
  - [x] 成功落盘后删除对应旧 WAL
  - [x] 多 WAL 恢复专项测试

## Phase 2.5 - DBImpl 重构（进入并发前）
- [x] 目标：降低 `DBImpl` 复杂度，先做“行为不变重构”
- [x] 拆分职责（按顺序）
  - [x] 提取 `ManifestManager`：Manifest 的 load/persist/append/replay/checkpoint
  - [x] 提取 `RecoveryLoader`：WAL 回放、SST 加载、`next_file_number` 初始化
  - [x] 提取 `CompactionEngine`：`MinorCompaction` 与 `L0->L1` compaction
- [x] `DBImpl` 收口
  - [x] `DBImpl` 只保留对外 API 与调度（`Put/Get/NewIterator`）
  - [x] 锁与线程入口统一放在 `DBImpl`，避免分散在多个模块
- [x] 模块封装收口（按顺序）
  - [x] `ManifestManager` 收口：内部持有 Manifest 状态，提供语义化接口（替代 `RecordEdit(op, id, level)`）
  - [x] `RecoveryLoader` 收口：减少对 `ManifestState`/回调细节暴露，收敛为恢复阶段接口
  - [x] `CompactionEngine` 收口：减少长参数与跨模块回调，收敛为 compaction 执行接口
  - [x] 清理 `DBImpl` 中跨模块桥接胶水（长参数 + lambda），保持行为不变
- [x] 重构约束（必须满足）
  - [x] 不改变 `SET/GET/DEL/SCAN` 对外语义
  - [x] 不改变现有 WAL/SST/Manifest 文件格式
  - [x] 每拆一块就同步更新对应文档与 TODO 勾选

## Phase 3 - 并发与后台任务（为网络化做准备）
- [ ] 明确 DB 并发策略
  - [ ] 写串行 + 多读并发边界文档化
  - [ ] 核心路径锁粒度收敛
  - [ ] 将当前 compaction 入口大锁细化为关键区段锁，降低性能开销
- [ ] 后台化落盘流程
  - [ ] `imm_` 后台 flush（minor compaction）
  - [ ] major compaction 后台线程
- [ ] 后台任务可观测
  - [ ] flush/compaction 触发次数与耗时

## Phase 4 - Compaction 能力升级
- [ ] 多层 compaction（L1 -> L2 -> ...）
  - [ ] 每层 size 阈值
  - [ ] overlap 选择策略（先简化版）
  - [ ] 输出文件层级与清理规则
- [ ] Compaction 统计
  - [ ] 输入/输出字节
  - [ ] write amplification 粗略统计

## Phase 5 - 写入能力增强（可追问点）
- [ ] WriteBatch（多 put/delete 原子提交）
- [ ] WAL 耐久性级别可配置（如每次 fsync / 周期 fsync）
- [ ] 相应正确性测试与性能对比

## Phase 6 - 网络层基础（先建立边界，再上并发）
- [ ] 服务层边界
  - [ ] 命令执行层与存储层解耦
  - [ ] 配置项：端口、数据目录、日志级别
- [ ] 协议 V1
  - [ ] 命令集合：`PING` / `SET` / `GET` / `DEL` / `SCAN`
  - [ ] 统一响应语义（成功/未命中/参数错/内部错）
  - [ ] 协议文档（便于调试与面试讲解）
- [ ] 网络协议测试 V1
  - [ ] 正常命令路径
  - [ ] 参数错误与异常断连

## Phase 7 - `epoll + 线程池` 并发网络模型
- [ ] IO/执行解耦
  - [ ] IO 线程处理 accept/read/write
  - [ ] 线程池执行命令
  - [ ] 响应回写与连接状态一致性
- [ ] 连接生命周期治理
  - [ ] 连接上下文（读缓冲/写缓冲/活跃时间）
  - [ ] 粘包/半包处理
  - [ ] 空闲超时与慢连接背压
  - [ ] 优雅关闭
- [ ] 并发网络测试
  - [ ] 多连接稳定性
  - [ ] 粘包/半包
  - [ ] 压力下错误率验证

## Phase 8 - 端到端证据链（面试加分关键）
- [ ] 端到端集成测试
  - [ ] client-server + restart recovery
  - [ ] 删除语义 + 扫描语义 + 恢复语义
- [ ] 性能与稳定性报告
  - [ ] 存储侧：Put/Get QPS 与延迟
  - [ ] 网络侧：连接数/QPS/P99/错误率
  - [ ] 每新增功能标注前后对比

## Phase 9 - 面试交付整理（最后做）
- [ ] README 升级为“系统说明书”
  - [ ] 架构图（写/读/恢复/网络）
  - [ ] 关键设计取舍与已知边界
  - [ ] 压测结果、复现实验步骤
- [ ] 演示脚本
  - [ ] 启动服务、写入、删除、重启恢复、范围扫描
  - [ ] 并发压测与关键指标展示

## Milestone（验收线）
- [ ] M1：完成 Phase 1-3（含 Phase 2.5，存储正确性 + 恢复 + 基础并发）
- [ ] M2：完成 Phase 6-7（可稳定运行的网络服务）
- [ ] M3：完成 Phase 8-9（有数据、有文档、可演示）

## 可选拓展（完成主线后再做）
- [ ] 后台 compaction 限速
- [ ] 前缀 Bloom filter 或每块 filter
- [ ] Manifest 轮转 / 压缩
