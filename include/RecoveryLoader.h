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
    RecoveryLoader(
        std::string db_path,
        ManifestManager &manifest_manager,
        std::vector<std::vector<SSTableReader *> > &levels);

    void RecoverFromWals(MemTable *mem) const;
    void LoadSSTables() const;
    void InitNextFileNumberFromDisk() const;

private:
    std::string db_path_;
    ManifestManager &manifest_manager_;
    std::vector<std::vector<SSTableReader *> > &levels_;
};

#endif // NOVAKV_RECOVERYLOADER_H
