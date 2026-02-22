//
// Created by 26708 on 2026/2/22.
//

#ifndef NOVAKV_RECOVERYLOADER_H
#define NOVAKV_RECOVERYLOADER_H

#include "ManifestManager.h"
#include "MemTable.h"
#include "SSTableReader.h"
#include <string>
#include <vector>

class RecoveryLoader {
public:
    explicit RecoveryLoader(std::string db_path);

    void RecoverFromWals(ManifestManager &manifest_manager, MemTable *mem) const;
    void LoadSSTables(ManifestManager &manifest_manager, std::vector<std::vector<SSTableReader *> > &levels) const;
    void InitNextFileNumberFromDisk(ManifestManager &manifest_manager) const;

private:
    std::string db_path_;
};

#endif // NOVAKV_RECOVERYLOADER_H
