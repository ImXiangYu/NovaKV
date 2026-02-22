//
// Created by 26708 on 2026/2/22.
//

#ifndef NOVAKV_RECOVERYLOADER_H
#define NOVAKV_RECOVERYLOADER_H

#include "ManifestManager.h"
#include "MemTable.h"
#include "SSTableReader.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class RecoveryLoader {
public:
    explicit RecoveryLoader(std::string db_path);

    void RecoverFromWals(
        ManifestState &state,
        MemTable *mem,
        const std::function<void(ManifestOp, uint64_t, uint32_t)> &record_manifest_edit) const;

    void LoadSSTables(
        ManifestState &state,
        std::vector<std::vector<SSTableReader *> > &levels,
        const std::function<bool()> &persist_manifest_state) const;

    void InitNextFileNumberFromDisk(ManifestState &state) const;

private:
    std::string db_path_;
};

#endif // NOVAKV_RECOVERYLOADER_H
