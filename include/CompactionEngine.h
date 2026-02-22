//
// Created by 26708 on 2026/2/22.
//

#ifndef NOVAKV_COMPACTIONENGINE_H
#define NOVAKV_COMPACTIONENGINE_H

#include "ManifestManager.h"
#include "MemTable.h"
#include "SSTableReader.h"
#include <string>
#include <vector>

class CompactionEngine {
public:
    explicit CompactionEngine(std::string db_path);

    void MinorCompaction(
        ManifestManager &manifest_manager,
        std::vector<std::vector<SSTableReader *> > &levels,
        MemTable *&mem,
        MemTable *&imm,
        uint64_t &active_wal_id) const;

    void CompactL0ToL1(
        ManifestManager &manifest_manager,
        std::vector<std::vector<SSTableReader *> > &levels) const;

private:
    bool HasVisibleValueInL1(
        const std::vector<std::vector<SSTableReader *> > &levels,
        const std::string &key) const;

    std::string db_path_;
};

#endif // NOVAKV_COMPACTIONENGINE_H
