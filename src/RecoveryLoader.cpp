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

RecoveryLoader::RecoveryLoader(
    std::string db_path,
    ManifestManager &manifest_manager,
    std::vector<std::vector<SSTableReader *> > &levels)
    : db_path_(std::move(db_path)),
      manifest_manager_(manifest_manager),
      levels_(levels) {
}

void RecoveryLoader::RecoverFromWals(MemTable *mem) const {
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
        const uint64_t id = std::stoull(stem);
        manifest_manager_.AddWal(id);
    }

    std::vector replay_ids(
        manifest_manager_.LiveWals().begin(),
        manifest_manager_.LiveWals().end());
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

void RecoveryLoader::LoadSSTables() const {
    LOG_INFO(std::string("LoadSSTables start"));

    for (auto &lv : levels_) {
        for (const auto *r : lv) delete r;
        lv.clear();
    }

    if (!manifest_manager_.SstLevels().empty()) {
        std::vector<std::pair<uint64_t, uint32_t> > entries(
            manifest_manager_.SstLevels().begin(),
            manifest_manager_.SstLevels().end());
        std::sort(entries.begin(),
                  entries.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });

        for (const auto &[id, level] : entries) {
            if (level >= levels_.size()) {
                LOG_ERROR("Manifest level out of range: " + std::to_string(level));
                continue;
            }

            const std::string path = db_path_ + "/" + std::to_string(id) + ".sst";
            if (!fs::exists(path)) {
                LOG_ERROR("Manifest SST missing: " + path);
                continue;
            }

            if (SSTableReader *reader = SSTableReader::Open(path)) {
                levels_[level].push_back(reader);
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
            levels_[0].push_back(reader);
            manifest_manager_.SetSstLevelWithoutEdit(id, 0);
        }
    }

    if (!manifest_manager_.SstLevels().empty()) {
        manifest_manager_.Persist();
    }

    LOG_INFO(std::string("LoadSSTables completed"));
}

void RecoveryLoader::InitNextFileNumberFromDisk() const {
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
    manifest_manager_.SetNextFileNumberWithoutEdit(max_id);
}
