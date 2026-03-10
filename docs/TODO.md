# NovaKV TODO（顺序执行版）

目标：从上到下逐步完成，最终形成“数据库内核 + 网络服务”双主线面试项目。  
执行原则：不跳阶段；每完成一个 Phase，再进入下一个。

## Phase 0 - 基线与规则（先把地基固定）
- [x] 冻结当前基线并记录现状
  - [x] 记录当前可通过的测试集
  - [x] 记录当前基线 QPS（Put/Get）
  - [x] 明确统一对外语义
  - [x] `GET` 对 tombstone 返回未命中
  - [x] 明确 `SET/GET/DEL/RSCAN` 语义定义文档

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
  - [x] 不改变 `SET/GET/DEL/RSCAN` 对外语义
  - [x] 不改变现有 WAL/SST/Manifest 文件格式
  - [x] 每拆一块就同步更新对应文档与 TODO 勾选

## Phase 3 - 并发与后台任务（已完成）

- [x] 明确 DB 并发策略
  - [x] 写串行 + 多读并发边界文档化
  - [x] `Put/Get/NewIterator/Compact` 锁顺序统一（避免死锁）
  - [x] 将当前 compaction 入口大锁细化为关键区段锁
- [x] 后台化落盘流程（先做最小闭环）
  - [x] `imm_` 后台 flush（minor compaction）
  - [x] flush 期间前台写入仅在 mem/imm 切换点短暂阻塞
  - [x] major compaction 保持手动/同步触发（本轮不引入额外后台线程）
- [x] 后台任务可观测
  - [x] flush/compaction 触发次数与耗时埋点
  - [x] 增加 `GetStatus` 接口与 `db_bench` 仪表盘展示

## Phase 4 - 网络服务主线（Redis 兼容版，当前主战场）

### 2026-03-09 ~ 2026-03-31 执行时间线（复试 / 求职双用途）

- [x] 2026-03-09（合并原 3/9 + 3/10 任务）
  - [x] 明确网络主链路接口：`CommandExecutor` / `Connection` / 线程池任务模型 / 回写路径
  - [x] 建立 `CommandExecutor` 最小闭环：打通 `SET/GET/DEL/RSCAN -> DBImpl -> RESPEncoder`
  - [x] 统一参数校验与错误返回：未知命令、参数个数错误先定成标准 RESP 错误格式
  - [x] 已导入线程池并确认最小接口可复用：满足“提交任务 / worker 执行 / 停机退出”
- [ ] 2026-03-10
  - [ ] 基础 Server 搭建：完成 non-blocking socket、`bind/listen`、`accept`
  - [ ] 搭建 `epoll` 骨架：事件注册、事件分发主循环、连接创建与销毁入口
  - [ ] 为每个连接挂接独立上下文：输入缓冲、输出缓冲、RESP parser、连接状态
- [ ] 2026-03-11
  - [ ] 打通单线程请求链路：读 socket -> 写入 `NetworkBuffer` -> RESP parse -> `CommandExecutor`
  - [ ] 打通响应回包：`RESPEncoder` 输出写入连接缓冲，并在可写事件中发送
  - [ ] 先完成单线程最小闭环，确保协议、命令、回包路径成立
- [ ] 2026-03-12
  - [ ] 接入线程池最小版本：IO 线程只负责收发与解析，DB 操作转交 worker
  - [ ] 明确任务对象边界：任务中至少包含连接标识、命令参数、执行所需上下文
  - [ ] worker 执行后生成响应内容，但先不急着做复杂生命周期治理
- [ ] 2026-03-13
  - [ ] 完成结果回写链路：worker 完成后将响应安全挂回连接输出缓冲
  - [ ] 完成写事件联动：需要发送时注册/开启 `EPOLLOUT`，发送完成后关闭无效写关注
  - [ ] 处理连接先关闭的基本保护，避免 worker 回写悬空连接
- [ ] 2026-03-14
  - [ ] 补关键边界：半包、粘包、一次读到多条命令、非法 RESP、未知命令
  - [ ] 补基础可观测日志：连接建立、命令执行失败、连接关闭、线程池任务异常
  - [ ] 形成第一版可演示的 `epoll + 线程池` 网络服务
- [ ] 2026-03-15
  - [ ] 对照复试线公布日做阶段收口：整理当前已完成模块与剩余问题
  - [ ] 更新 `docs/TODO.md` 勾选状态，并写一份阶段复盘文档
  - [ ] 若主链路已通：开始准备 `redis-cli` 联调脚本与演示口径
- [ ] 2026-03-16 ~ 2026-03-22
  - [ ] 完成优雅关闭：`SIGINT/SIGTERM`、停止 accept、停止 worker、DB 安全析构
  - [ ] 完成 `redis-cli` 兼容性验证，并修正协议与返回格式细节
  - [ ] 处理连接生命周期、任务未完成断连、异常断连等并发边界
  - [ ] 形成稳定的 Phase 4 版本，目标是达到 M2
- [ ] 2026-03-23 ~ 2026-03-31
  - [ ] 进入 Phase 5/6 的面试交付整理：补最小端到端证据链、README、演示脚本
  - [ ] 准备两套表达版本：复试版（突出设计与取舍）/ 求职版（突出工程与并发）
  - [ ] 手动完成一次服务侧指标记录，为后续问答准备证据
- [ ] 时间线执行原则
  - [ ] 优先级顺序固定为：`CommandExecutor` > 单线程闭环 > 线程池接入 > 优雅关闭 / `redis-cli` 联调 > 文档与证据链
  - [ ] 每完成一个子阶段，立即回看本文件勾选状态，避免实现与 TODO 脱节

- [x] RESP 协议实现（暗号对接）
  - [x] 实现 RESP Parser：支持 `*` (数组) 和 `$` (大块字符串) 的状态机解析
  - [x] 解决“半包”问题：Parser 需支持断点续传，数据不足时保留状态
  - [x] 实现 RESP Encoder：支持 `+OK`、`-ERR`、`$Bulk` 等响应格式
- [ ] 命令分发层（Command Dispatcher）
  - [ ] 建立 `CommandExecutor`：将 `SET/GET/DEL/RSCAN` 路由至 `DBImpl` 对应接口
  - [ ] 异常处理：对不支持的命令返回 Redis 标准错误格式
- [ ] 高并发网络引擎（epoll）
  - [ ] 基础 Server 搭建：Non-blocking Socket + bind/listen
  - [ ] epoll 循环：实现 Acceptor 处理新连接，IO 线程处理读写事件
  - [ ] 连接上下文管理：为每个 Client 维护独立的读写缓冲区（Buffer）与 parser / 输出状态
- [ ] 线程池并发调度
  - [x] 线程池基础组件已导入：支持任务提交、worker 执行、析构时回收线程
  - [ ] 任务解耦：IO 线程仅负责收发，业务逻辑（DB 操作）丢入线程池
  - [ ] 结果回写：业务线程执行完后，将响应内容挂载至连接的写缓冲，由 epoll 触发发送
- [ ] 生产级治理（面试加分项）
  - [ ] 优雅关闭：处理 `SIGINT/SIGTERM`，确保 Server 停止前 DB 安全析构
  - [ ] 兼容性验证：使用官方 `redis-cli` 成功连接并进行读写操作

## Phase 5 - 端到端证据链（先做最小可展示版）

### 执行原则

- [ ] Phase 5 不追求一次性补全全部测试矩阵，先做“能支撑复试 / 首轮面试讲解”的代表性证据
- [ ] 优先补最能体现系统闭环的场景：client-server、重启恢复、删除语义、扫描语义、异常参数
- [ ] 所有测试与验证命令只写方案和记录，不在当前工作区直接执行

- [ ] 端到端集成测试
  - [ ] P0：client-server 基本读写闭环
  - [ ] P0：restart recovery（重启后数据仍可读）
  - [ ] P0：删除语义 + 扫描语义 + 恢复语义
  - [ ] P1：参数错误与异常断连
- [ ] 并发网络专项测试
  - [ ] P1：多连接稳定性（长连接 + 短连接）
  - [ ] P1：粘包/半包与大包体场景
  - [ ] P2：压力下错误率与超时率验证
- [ ] 性能与稳定性记录（轻量）
  - [x] 存储侧：Put/Get QPS 与延迟
  - [ ] 服务侧：连接数/QPS/P99/错误率
  - [ ] 网络服务阶段性版本前后对比

## Phase 6 - 面试交付整理（复试 / 求职双版本）

### 执行原则

- [ ] 先做“能讲清楚”的交付，再补“更完整”的文档包装
- [ ] 复试版本强调系统设计、取舍和恢复/并发语义
- [ ] 求职版本强调工程实现、模块边界、`epoll + 线程池` 与可观测性

- [ ] README 升级为“系统说明书”
  - [ ] P0：架构图（写 / 读 / 恢复 / 网络服务）
  - [ ] P0：关键设计取舍与已知边界
  - [ ] P1：复现实验步骤（含并发压测）
- [ ] 演示脚本
  - [ ] P0：启动服务、写入、删除、重启恢复、范围扫描
  - [ ] P1：并发压测与关键指标展示
  - [ ] P0：准备 5 分钟复试讲解版
  - [ ] P0：准备 10 分钟求职项目讲解版

## Milestone（验收线）

- [x] M1：完成 Phase 1-3（存储正确性 + 恢复 + 后台异步落盘 + 可观测性）
- [ ] M2：完成 Phase 4（可稳定运行的 `epoll + 线程池` 网络服务，支持 `redis-cli` 基本联调）
- [ ] M3：完成 Phase 5 的 P0 + Phase 6 的 P0（有最小证据链、有 README 主体、有演示脚本、可用于复试 / 首轮面试）
- [ ] M4：完成 Phase 5-6 全量收口（有测试、有数据、有文档、可演示）

## 可选拓展（完成主线后再做）

- [ ] 多层 compaction（L1 -> L2 -> ...）
- [ ] WriteBatch（多 put/delete 原子提交）
- [ ] WAL 耐久性级别可配置（如每次 fsync / 周期 fsync）
- [ ] 后台 compaction 限速
- [ ] 前缀 Bloom filter 或每块 filter
- [ ] Manifest 轮转 / 压缩
