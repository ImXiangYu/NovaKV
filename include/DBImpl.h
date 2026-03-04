//
// Created by 26708 on 2026/2/7.
//

#ifndef NOVAKV_DBIMPL_H
#define NOVAKV_DBIMPL_H

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "CompactionEngine.h"
#include "DBIterator.h"
#include "ManifestManager.h"
#include "MemTable.h"
#include "RecoveryLoader.h"
#include "SSTableReader.h"

class DBImpl {
 public:
  explicit DBImpl(std::string db_path);
  ~DBImpl();

  void Put(const std::string& key, const ValueRecord& value);
  bool Get(const std::string& key, ValueRecord& value) const;
  void CompactL0ToL1();
  size_t LevelSize(size_t level) const;

  // 迭代器
  std::unique_ptr<DBIterator> NewIterator();

 private:
  void MinorCompaction();
  // 后台进程
  void BackgroundLoop();

  std::string db_path_;
  ManifestManager manifest_manager_;

  // 磁盘层：已打开的 SST 列表
  // levels_[0] 是 L0，levels_[1] 是 L1
  std::vector<std::vector<SSTableReader*>> levels_;

  CompactionEngine compaction_engine_;
  RecoveryLoader recovery_loader_;

  // 内存层：解耦后的指针
  MemTable* mem_ = nullptr;
  MemTable* imm_ = nullptr;

  // 维护active_wal_id_
  uint64_t active_wal_id_ = 0;
  // imm_对应的WAL ID
  uint64_t imm_wal_id_ = 0;

  // 写串行
  std::mutex write_mu_;
  // 全局状态共享锁
  mutable std::shared_mutex state_mu_;

  // 后台线程，用于MinorCompaction
  std::thread background_thread_;
  // cv，用来通知后台进程干活
  std::condition_variable_any bg_cv_;
  // 停止标志位，析构时用
  bool bg_stopped_;
  // 一个简单的标志位，表示是否有落盘任务待处理
  bool bg_compaction_scheduled_;
};

#endif  // NOVAKV_DBIMPL_H
