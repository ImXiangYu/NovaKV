//
// Created by 26708 on 2026/2/22.
//

#include "RecoveryLoader.h"
#include "Logger.h"
#include "WalHandler.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>

namespace fs = std::filesystem;

RecoveryLoader::RecoveryLoader(std::string db_path) : db_path_(std::move(db_path)) {
}

void RecoveryLoader::RecoverFromWals(
    ManifestState &state,
    MemTable *mem,
    const std::function<void(ManifestOp, uint64_t, uint32_t)> &record_manifest_edit) const {
    LOG_INFO(std::string("Recover from wals start"));

    for (auto &entry : fs::directory_iterator(db_path_)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".wal") {
            continue;
        }
        const std::string stem = entry.path().stem().string();
        if (!std::all_of(stem.begin(),
                         stem.end(),
                         [](const unsigned char c) { return std::isdigit(c); })) {
            continue;
        }
        if (const uint64_t id = std::stoull(stem); state.live_wals.insert(id).second) {
            record_manifest_edit(ManifestOp::AddWAL, id, 0);
        }
    }

    std::vector replay_ids(
        state.live_wals.begin(),
        state.live_wals.end());
    std::sort(replay_ids.begin(), replay_ids.end());

    for (const uint64_t id : replay_ids) {
        const std::string wal_path = db_path_ + "/" + std::to_string(id) + ".wal";
        if (!fs::exists(wal_path)) {
            LOG_WARN(std::string("Manifest WAL missing on disk: ") + wal_path);
            continue;
        }
        WalHandler handler(wal_path);
        handler.LoadLog([mem](const ValueType type, const std::string &k, const std::string &v) {
            mem->ApplyWithoutWal(k, {type, v});
        });
    }
    LOG_INFO(std::string("Recover from wals completed"));
}

void RecoveryLoader::LoadSSTables(
    ManifestState &state,
    std::vector<std::vector<SSTableReader *> > &levels,
    const std::function<bool()> &persist_manifest_state) const {
    LOG_INFO(std::string("LoadSSTables start"));

    for (auto &lv : levels) {
        for (const auto *r : lv) delete r;
        lv.clear();
    }

    if (!state.sst_levels.empty()) {
        std::vector<std::pair<uint64_t, uint32_t> > entries(
            state.sst_levels.begin(),
            state.sst_levels.end());
        std::sort(entries.begin(),
                  entries.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });

        for (const auto &[id, level] : entries) {
            if (level >= levels.size()) {
                LOG_ERROR("Manifest level out of range: " + std::to_string(level));
                continue;
            }

            const std::string path = db_path_ + "/" + std::to_string(id) + ".sst";
            if (!fs::exists(path)) {
                LOG_ERROR("Manifest SST missing: " + path);
                continue;
            }

            if (SSTableReader *reader = SSTableReader::Open(path)) {
                levels[level].push_back(reader);
            } else {
                LOG_ERROR("Failed to open manifest SST: " + path);
            }
        }

        LOG_INFO(std::string("LoadSSTables completed"));
        return;
    }

    std::vector<std::pair<int, std::string> > sstables;
    for (auto &entry : fs::directory_iterator(db_path_)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".sst") continue;
        const std::string stem = entry.path().stem().string();
        if (!std::all_of(stem.begin(),
                         stem.end(),
                         [](unsigned char c) { return std::isdigit(c); })) {
            continue;
        }
        const uint64_t id = std::stoull(stem);
        sstables.emplace_back(id, entry.path().string());
    }
    std::sort(sstables.begin(), sstables.end());

    for (const auto &[id, path] : sstables) {
        if (SSTableReader *reader = SSTableReader::Open(path)) {
            levels[0].push_back(reader);
            state.sst_levels[id] = 0;
        }
    }

    if (!state.sst_levels.empty()) {
        persist_manifest_state();
    }

    LOG_INFO(std::string("LoadSSTables completed"));
}

void RecoveryLoader::InitNextFileNumberFromDisk(ManifestState &state) const {
    int max_id = 0;
    for (auto &entry : fs::directory_iterator(db_path_)) {
        if (entry.is_regular_file()) {
            if (entry.path().extension() == ".sst" || entry.path().extension() == ".wal") {
                std::string stem = entry.path().stem().string();
                if (!std::all_of(stem.begin(),
                                 stem.end(),
                                 [](const unsigned char c) { return std::isdigit(c); })) {
                    continue;
                }
                int id = std::stoi(stem);
                max_id = std::max(max_id, id);
            }
        }
    }
    state.next_file_number = max_id;
}
