//
// Created by 26708 on 2026/2/22.
//

#include "CompactionEngine.h"

#include <filesystem>
#include <map>
#include <new>
#include <utility>

#include "FileFormats.h"
#include "Logger.h"
#include "SSTableBuilder.h"

namespace fs = std::filesystem;

CompactionEngine::CompactionEngine(
    std::string db_path, ManifestManager& manifest_manager,
    std::vector<std::vector<SSTableReader*> >& levels)
    : db_path_(std::move(db_path)),
      manifest_manager_(manifest_manager),
      levels_(levels) {}

void CompactionEngine::CompactL0ToL1() const {
  if (levels_[0].empty()) return;
  std::map<std::string, ValueRecord> mp;
  for (auto it = levels_[0].rbegin(); it != levels_[0].rend(); ++it) {
    (*it)->ForEach([&](const std::string& key, const std::string& value,
                       const ValueType type) {
      mp.try_emplace(key, ValueRecord{type, value});
    });
  }

  std::vector<uint64_t> l0_input_ids;
  for (const auto& [id, level] : manifest_manager_.SstLevels()) {
    if (level == 0) l0_input_ids.push_back(id);
  }

  auto consume_l0 = [&]() {
    for (const auto* r : levels_[0]) {
      delete r;
    }
    levels_[0].clear();

    for (const uint64_t id : l0_input_ids) {
      manifest_manager_.RemoveSst(id);
      fs::remove(fs::path(db_path_) / (std::to_string(id) + ".sst"));
    }
  };

  if (mp.empty()) {
    consume_l0();
    return;
  }

  const uint64_t new_sst_id = manifest_manager_.AllocateFileNumber();
  std::string new_sst_path =
      db_path_ + "/" + std::to_string(new_sst_id) + ".sst";

  WritableFile file(new_sst_path);
  SSTableBuilder builder(&file);

  bool has_output = false;
  for (const auto& [key, record] : mp) {
    if (record.type == ValueType::kValue) {
      builder.Add(key, record.value, ValueType::kValue);
      has_output = true;
    } else {
      if (HasVisibleValueInL1(key)) {
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

  if (SSTableReader* reader = SSTableReader::Open(new_sst_path)) {
    LOG_INFO(std::string("SSTable created: ") + new_sst_path);
    levels_[1].push_back(reader);
    manifest_manager_.AddSst(new_sst_id, 1);
    consume_l0();
    return;
  }

  LOG_ERROR(std::string("Failed to open SSTable: ") + new_sst_path);
  fs::remove(new_sst_path);
}

bool CompactionEngine::HasVisibleValueInL1(const std::string& key) const {
  if (levels_.size() <= 1) return false;
  for (size_t i = levels_[1].size(); i-- > 0;) {
    ValueRecord rec{ValueType::kDeletion, ""};
    if (levels_[1][i]->GetRecord(key, &rec)) {
      if (rec.type == ValueType::kDeletion) {
        return false;
      }
      return true;
    }
  }
  return false;
}

bool CompactionEngine::PrepareMinor(MemTable*& mem, MemTable*& imm,
                                    uint64_t& active_wal_id,
                                    MinorCtx& ctx) const {
  if (mem == nullptr) {
    LOG_ERROR("PrepareMinor failed: mem is null.");
    return false;
  }
  if (imm != nullptr) {
    LOG_ERROR("PrepareMinor failed: imm already exists.");
    return false;
  }
  LOG_INFO("Minor Compaction start.");
  const uint64_t old_wal_id = active_wal_id;
  std::string old_wal_path = mem->GetWalPath();

  imm = mem;
  LOG_INFO(std::string("Immutable MemTable items: ") +
           std::to_string(imm->Count()));

  const uint64_t new_sst_id = manifest_manager_.AllocateFileNumber();
  std::string sst_path = db_path_ + "/" + std::to_string(new_sst_id) + ".sst";
  ctx.flushing_imm = imm;
  ctx.new_sst_id = new_sst_id;
  ctx.new_sst_path = sst_path;
  ctx.old_wal_id = old_wal_id;
  ctx.old_wal_path = old_wal_path;
  return true;
}

SSTableReader* CompactionEngine::BuildMinorSST(const MinorCtx& ctx) const {
  if (ctx.flushing_imm == nullptr) {
    LOG_ERROR("BuildMinorSST failed: flushing_imm is null.");
    return nullptr;
  }
  if (ctx.new_sst_path.empty()) {
    LOG_ERROR("BuildMinorSST failed: new_sst_path is empty.");
    return nullptr;
  }
  if (fs::exists(ctx.new_sst_path) && !fs::remove(ctx.new_sst_path)) {
    LOG_ERROR(std::string("BuildMinorSST failed: cannot remove stale file: ") +
              ctx.new_sst_path);
    return nullptr;
  }

  WritableFile file(ctx.new_sst_path);
  SSTableBuilder builder(&file);

  auto it = ctx.flushing_imm->GetIterator();
  while (it.Valid()) {
    builder.Add(it.key(), it.value().value, it.value().type);
    it.Next();
  }
  builder.Finish();
  file.Flush();
  LOG_INFO(std::string("SSTable created: ") + ctx.new_sst_path);

  SSTableReader* reader = SSTableReader::Open(ctx.new_sst_path);
  if (reader == nullptr) {
    LOG_ERROR(std::string("BuildMinorSST failed: cannot open sstable: ") +
              ctx.new_sst_path);
    fs::remove(ctx.new_sst_path);
    return nullptr;
  }
  return reader;
}

bool CompactionEngine::InstallMinor(MemTable*& mem, MemTable*& imm,
                                    uint64_t& active_wal_id,
                                    const MinorCtx& ctx, SSTableReader* reader,
                                    bool& need_l0_compact) const {
  if (mem == nullptr) {
    LOG_ERROR("InstallMinor failed: mem is null.");
    return false;
  }
  if (ctx.flushing_imm == nullptr) {
    LOG_ERROR("InstallMinor failed: flushing_imm is null.");
    return false;
  }
  if (reader == nullptr) {
    LOG_ERROR("InstallMinor failed: reader is null.");
    return false;
  }
  if (imm != ctx.flushing_imm) {
    LOG_ERROR("InstallMinor failed: imm does not match flushing_imm.");
    return false;
  }

  levels_[0].push_back(reader);
  manifest_manager_.AddSst(ctx.new_sst_id, 0);

  const uint64_t new_wal_id = manifest_manager_.AllocateFileNumber();
  std::string new_wal = db_path_ + "/" + std::to_string(new_wal_id) + ".wal";
  MemTable* new_mem = new (std::nothrow) MemTable(new_wal);
  if (new_mem == nullptr) {
    LOG_ERROR(
        std::string("InstallMinor failed: cannot allocate MemTable for ") +
        new_wal);
    levels_[0].pop_back();
    manifest_manager_.RemoveSst(ctx.new_sst_id);
    fs::remove(ctx.new_sst_path);
    return false;
  }

  active_wal_id = new_wal_id;
  mem = new_mem;
  if (!manifest_manager_.AddWal(new_wal_id)) {
    LOG_ERROR(std::string("InstallMinor warning: AddWal failed for wal id ") +
              std::to_string(new_wal_id));
  }

  if (fs::exists(ctx.old_wal_path) && fs::remove(ctx.old_wal_path)) {
    if (!manifest_manager_.RemoveWal(ctx.old_wal_id)) {
      LOG_ERROR(
          std::string("InstallMinor warning: RemoveWal failed for wal id ") +
          std::to_string(ctx.old_wal_id));
    }
    LOG_INFO(std::string("Removed old wal file: ") + ctx.old_wal_path);
  } else {
    LOG_ERROR(std::string("InstallMinor warning: old WAL remove failed: ") +
              ctx.old_wal_path);
  }

  need_l0_compact = levels_[0].size() >= 2;
  delete imm;
  imm = nullptr;
  return true;
}
