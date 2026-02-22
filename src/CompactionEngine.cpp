//
// Created by 26708 on 2026/2/22.
//

#include "CompactionEngine.h"
#include "FileFormats.h"
#include "Logger.h"
#include "SSTableBuilder.h"
#include <filesystem>
#include <map>
#include <utility>

namespace fs = std::filesystem;

CompactionEngine::CompactionEngine(std::string db_path) : db_path_(std::move(db_path)) {
}

void CompactionEngine::MinorCompaction(
    ManifestManager &manifest_manager,
    std::vector<std::vector<SSTableReader *> > &levels,
    MemTable *&mem,
    MemTable *&imm,
    uint64_t &active_wal_id) const {
    LOG_INFO("Minor Compaction triggered...");
    const uint64_t old_wal_id = active_wal_id;
    std::string old_wal_path = mem->GetWalPath();

    imm = mem;
    LOG_INFO(std::string("Immutable MemTable items: ") + std::to_string(imm->Count()));

    const uint64_t new_sst_id = manifest_manager.AllocateFileNumber();
    std::string sst_path = db_path_ + "/" + std::to_string(new_sst_id) + ".sst";
    bool sst_ready = false;

    {
        WritableFile file(sst_path);
        SSTableBuilder builder(&file);

        auto it = imm->GetIterator();
        while (it.Valid()) {
            builder.Add(it.key(), it.value().value, it.value().type);
            it.Next();
        }
        builder.Finish();
        file.Flush();
        LOG_INFO(std::string("SSTable created: ") + sst_path);
    }

    if (SSTableReader *level = SSTableReader::Open(sst_path)) {
        levels[0].push_back(level);
        manifest_manager.AddSst(new_sst_id, 0);
        sst_ready = true;
    } else {
        LOG_ERROR(std::string("Failed to open SSTable: ") + sst_path);
    }

    const uint64_t new_wal_id = manifest_manager.AllocateFileNumber();
    active_wal_id = new_wal_id;
    std::string new_wal = db_path_ + "/" + std::to_string(new_wal_id) + ".wal";
    mem = new MemTable(new_wal);
    manifest_manager.AddWal(new_wal_id);

    if (sst_ready) {
        if (fs::exists(old_wal_path) && fs::remove(old_wal_path)) {
            manifest_manager.RemoveWal(old_wal_id);
            LOG_INFO(std::string("Removed old wal file: ") + old_wal_path);
        } else {
            LOG_WARN(std::string("WAL path does not exist: ") + old_wal_path);
        }
    }

    if (levels[0].size() >= 2) {
        CompactL0ToL1(manifest_manager, levels);
    }

    delete imm;
    imm = nullptr;
}

void CompactionEngine::CompactL0ToL1(
    ManifestManager &manifest_manager,
    std::vector<std::vector<SSTableReader *> > &levels) const {
    if (levels[0].empty()) return;
    std::map<std::string, ValueRecord> mp;
    for (auto it = levels[0].rbegin(); it != levels[0].rend(); ++it) {
        (*it)->ForEach([&](const std::string &key, const std::string &value, const ValueType type) {
            mp.try_emplace(key, ValueRecord{type, value});
        });
    }

    std::vector<uint64_t> l0_input_ids;
    for (const auto &[id, level] : manifest_manager.SstLevels()) {
        if (level == 0) l0_input_ids.push_back(id);
    }

    auto consume_l0 = [&]() {
        for (const auto *r : levels[0]) {
            delete r;
        }
        levels[0].clear();

        for (const uint64_t id : l0_input_ids) {
            manifest_manager.RemoveSst(id);
            fs::remove(fs::path(db_path_) / (std::to_string(id) + ".sst"));
        }
    };

    if (mp.empty()) {
        consume_l0();
        return;
    }

    const uint64_t new_sst_id = manifest_manager.AllocateFileNumber();
    std::string new_sst_path = db_path_ + "/" + std::to_string(new_sst_id) + ".sst";

    WritableFile file(new_sst_path);
    SSTableBuilder builder(&file);

    bool has_output = false;
    for (const auto &[key, record] : mp) {
        if (record.type == ValueType::kValue) {
            builder.Add(key, record.value, ValueType::kValue);
            has_output = true;
        } else {
            if (HasVisibleValueInL1(levels, key)) {
                builder.Add(key, "", ValueType::kDeletion);
                has_output = true;
            }
        }
    }
    builder.Finish();
    file.Flush();

    if (!has_output) {
        fs::remove(new_sst_path);
        consume_l0();
        return;
    }

    if (SSTableReader *reader = SSTableReader::Open(new_sst_path)) {
        LOG_INFO(std::string("SSTable created: ") + new_sst_path);
        levels[1].push_back(reader);
        manifest_manager.AddSst(new_sst_id, 1);
        consume_l0();
        return;
    }

    LOG_ERROR(std::string("Failed to open SSTable: ") + new_sst_path);
    fs::remove(new_sst_path);
}

bool CompactionEngine::HasVisibleValueInL1(
    const std::vector<std::vector<SSTableReader *> > &levels,
    const std::string &key) const {
    if (levels.size() <= 1) return false;
    for (size_t i = levels[1].size(); i-- > 0;) {
        ValueRecord rec{ValueType::kDeletion, ""};
        if (levels[1][i]->GetRecord(key, &rec)) {
            if (rec.type == ValueType::kDeletion) {
                return false;
            }
            return true;
        }
    }
    return false;
}
