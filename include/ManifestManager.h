//
// Created by 26708 on 2026/2/22.
//

#ifndef NOVAKV_MANIFESTMANAGER_H
#define NOVAKV_MANIFESTMANAGER_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

enum class ManifestOp : uint8_t {
    SetNextFileNumber = 1, AddSST = 2, DelSST = 3, AddWAL = 4, DelWAL = 5
};

struct ManifestState {
    uint64_t next_file_number = 0;
    std::unordered_map<uint64_t, uint32_t> sst_levels; // file_number -> level
    std::unordered_set<uint64_t> live_wals;            // 为多 WAL 恢复闭环预留
};

class ManifestManager {
public:
    explicit ManifestManager(std::string db_path);

    bool LoadState(ManifestState &state) const;
    bool PersistState(const ManifestState &state) const;

    bool AppendEdit(ManifestOp op, uint64_t id, uint32_t level = 0) const;
    bool ReplayLog(ManifestState &state) const;
    static bool ApplyEdit(ManifestState &state, ManifestOp op, uint64_t id, uint32_t level = 0);

    void RecordEdit(ManifestState &state,
                    uint32_t &edits_since_checkpoint,
                    ManifestOp op,
                    uint64_t id,
                    uint32_t level = 0) const;
    void MaybeCheckpoint(ManifestState &state, uint32_t &edits_since_checkpoint) const;
    bool TruncateLog() const;

private:
    std::string db_path_;
};

#endif // NOVAKV_MANIFESTMANAGER_H
