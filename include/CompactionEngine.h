//
// Created by 26708 on 2026/2/22.
//

#ifndef NOVAKV_COMPACTIONENGINE_H
#define NOVAKV_COMPACTIONENGINE_H

#include <string>
#include <vector>

#include "ManifestManager.h"
#include "MemTable.h"
#include "SSTableReader.h"

class CompactionEngine {
 public:
  CompactionEngine(std::string db_path, ManifestManager& manifest_manager,
                   std::vector<std::vector<SSTableReader*> >& levels);

  void MinorCompaction(MemTable*& mem, MemTable*& imm,
                       uint64_t& active_wal_id) const;

  void CompactL0ToL1() const;

 private:
  bool HasVisibleValueInL1(const std::string& key) const;

  // 把完整的MinorCompaction拆分成三阶段分别加锁
  // 这样可以保证最耗时的写SST不在锁中
  // CompactionEngine.h
  struct MinorCtx {
    MemTable* flushing_imm = nullptr;
    uint64_t old_wal_id = 0;
    std::string old_wal_path;
    uint64_t new_sst_id = 0;
    std::string new_sst_path;
  };

  bool PrepareMinor(MemTable*& mem, MemTable*& imm, uint64_t& active_wal_id,
                    MinorCtx& ctx) const;                   // 短操作
  SSTableReader* BuildMinorSST(const MinorCtx& ctx) const;  // 长 IO
  bool InstallMinor(const MinorCtx& ctx, SSTableReader* reader,
                    bool& need_l0_compact) const;  // 短操作

  std::string db_path_;
  ManifestManager& manifest_manager_;
  std::vector<std::vector<SSTableReader*> >& levels_;
};

#endif  // NOVAKV_COMPACTIONENGINE_H
