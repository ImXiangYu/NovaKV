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
  CompactionEngine(std::string db_path, ManifestManager &manifest_manager,
                   std::vector<std::vector<SSTableReader *> > &levels);

  void MinorCompaction(MemTable *&mem, MemTable *&imm,
                       uint64_t &active_wal_id) const;

  void CompactL0ToL1() const;

 private:
  bool HasVisibleValueInL1(const std::string &key) const;

  std::string db_path_;
  ManifestManager &manifest_manager_;
  std::vector<std::vector<SSTableReader *> > &levels_;
};

#endif  // NOVAKV_COMPACTIONENGINE_H
