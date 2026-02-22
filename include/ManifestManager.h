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

    bool Load();
    bool Persist() const;
    bool ReplayLog();

    uint64_t AllocateFileNumber();
    void AddWal(uint64_t wal_id);
    void RemoveWal(uint64_t wal_id);
    void AddSst(uint64_t file_number, uint32_t level);
    void RemoveSst(uint64_t file_number);

    const ManifestState &State() const;
    ManifestState &MutableState();

private:
    bool AppendEdit(ManifestOp op, uint64_t id, uint32_t level = 0) const;
    bool ApplyEdit(ManifestOp op, uint64_t id, uint32_t level = 0);
    void RecordEdit(ManifestOp op, uint64_t id, uint32_t level = 0);
    void MaybeCheckpoint();
    bool TruncateLog() const;
    bool LoadState(ManifestState &state) const;
    bool PersistState(const ManifestState &state) const;

    std::string db_path_;
    uint32_t edits_since_checkpoint_ = 0;
    ManifestState state_;
};

#endif // NOVAKV_MANIFESTMANAGER_H
