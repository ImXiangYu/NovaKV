//
// Created by 26708 on 2026/2/22.
//

#include "CompactionEngine.h"

#include <algorithm>
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

bool CompactionEngine::PrepareL0ToL1(L0ToL1Ctx& ctx) const {
  ctx = L0ToL1Ctx{};
  if (levels_[0].empty()) {
    return false;
  }

  for (auto it = levels_[0].rbegin(); it != levels_[0].rend(); ++it) {
    (*it)->ForEach([&](const std::string& key, const std::string& value,
                       const ValueType type) {
      ctx.merged_l0_records.try_emplace(key, ValueRecord{type, value});
    });
  }

  for (const auto& [id, level] : manifest_manager_.SstLevels()) {
    if (level == 0) {
      ctx.l0_input_ids.push_back(id);
    }
  }
  ctx.expected_l0_reader_count = levels_[0].size();
  if (ctx.l0_input_ids.size() != ctx.expected_l0_reader_count) {
    LOG_ERROR("PrepareL0ToL1 failed: L0 reader/id count mismatch.");
    return false;
  }

  if (ctx.merged_l0_records.empty()) {
    return true;
  }

  // 和旧逻辑保持一致：只要输入非空就先占用一个 file number。
  ctx.new_sst_id = manifest_manager_.AllocateFileNumber();
  ctx.new_sst_path = db_path_ + "/" + std::to_string(ctx.new_sst_id) + ".sst";

  for (const auto& [key, record] : ctx.merged_l0_records) {
    if (record.type == ValueType::kValue) {
      ctx.output_records.emplace(key, record);
      continue;
    }
    if (HasVisibleValueInL1(key)) {
      ctx.output_records.emplace(key, ValueRecord{ValueType::kDeletion, ""});
    }
  }

  ctx.has_output = !ctx.output_records.empty();
  return true;
}

SSTableReader* CompactionEngine::BuildL0ToL1SST(const L0ToL1Ctx& ctx) const {
  if (!ctx.has_output) {
    return nullptr;
  }
  if (ctx.new_sst_path.empty()) {
    LOG_ERROR("BuildL0ToL1SST failed: new_sst_path is empty.");
    return nullptr;
  }
  if (fs::exists(ctx.new_sst_path) && !fs::remove(ctx.new_sst_path)) {
    LOG_ERROR(
        std::string("BuildL0ToL1SST failed: cannot remove stale file: ") +
        ctx.new_sst_path);
    return nullptr;
  }

  WritableFile file(ctx.new_sst_path);
  SSTableBuilder builder(&file);
  for (const auto& [key, record] : ctx.output_records) {
    builder.Add(key, record.value, record.type);
  }
  builder.Finish();
  file.Flush();

  SSTableReader* reader = SSTableReader::Open(ctx.new_sst_path);
  if (reader == nullptr) {
    LOG_ERROR(std::string("BuildL0ToL1SST failed: cannot open sstable: ") +
              ctx.new_sst_path);
    fs::remove(ctx.new_sst_path);
    return nullptr;
  }
  LOG_INFO(std::string("SSTable created: ") + ctx.new_sst_path);
  return reader;
}

bool CompactionEngine::InstallL0ToL1(const L0ToL1Ctx& ctx,
                                     SSTableReader* reader) const {
  if (levels_[0].size() != ctx.expected_l0_reader_count) {
    LOG_ERROR("InstallL0ToL1 failed: L0 reader count changed during build.");
    return false;
  }

  std::vector<uint64_t> current_l0_ids;
  for (const auto& [id, level] : manifest_manager_.SstLevels()) {
    if (level == 0) {
      current_l0_ids.push_back(id);
    }
  }

  auto expected_l0_ids = ctx.l0_input_ids;
  std::sort(expected_l0_ids.begin(), expected_l0_ids.end());
  std::sort(current_l0_ids.begin(), current_l0_ids.end());
  if (expected_l0_ids != current_l0_ids) {
    LOG_ERROR("InstallL0ToL1 failed: L0 inputs changed during build.");
    return false;
  }

  auto consume_l0 = [&]() {
    for (const auto* r : levels_[0]) {
      delete r;
    }
    levels_[0].clear();

    for (const uint64_t id : ctx.l0_input_ids) {
      manifest_manager_.RemoveSst(id);
      fs::remove(fs::path(db_path_) / (std::to_string(id) + ".sst"));
    }
  };

  if (ctx.merged_l0_records.empty() || !ctx.has_output) {
    consume_l0();
    return true;
  }

  if (reader == nullptr) {
    LOG_ERROR("InstallL0ToL1 failed: reader is null.");
    return false;
  }

  levels_[1].push_back(reader);
  manifest_manager_.AddSst(ctx.new_sst_id, 1);
  consume_l0();
  return true;
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

