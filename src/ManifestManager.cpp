//
// Created by 26708 on 2026/2/22.
//

#include "ManifestManager.h"
#include "Logger.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr uint32_t kManifestMagic = 0x12345678; // 自定义
constexpr uint32_t kManifestVersion = 1;
constexpr uint32_t kManifestCheckpointThreshold = 100;
} // 匿名

ManifestManager::ManifestManager(std::string db_path) : db_path_(std::move(db_path)) {
}

/*
   参数语义统一为：
       SetNextFileNumber: id = next_file_number，level 忽略
       AddSST: id = file_number，level = level
       DelSST: id = file_number
       AddWAL/DelWAL: id = wal_id
   日志记录格式建议固定为：
       magic(u32) + version(u32) + op(u8) + payload_size(u32)
       payload 按 op 写入（u64 或 u64+u32）
    payload:
        SetNextFileNumber: u64 next_file_number
        AddSST: u64 file_number + u32 level
        DelSST: u64 file_number
        AddWAL/DelWAL: u64 wal_id
*/
bool ManifestManager::AppendEdit(const ManifestOp op, const uint64_t id, const uint32_t level) const {
    if (!fs::exists(db_path_) || !fs::is_directory(db_path_)) {
        LOG_ERROR("DB Path error: " + db_path_);
        return false;
    }

    // MANIFEST.log
    const fs::path p = fs::path(db_path_) / "MANIFEST.log";

    if (std::ofstream ofs(p, std::ios::binary | std::ios::app); ofs) {
        uint32_t payload_size = 0;
        switch (op) {
            case ManifestOp::SetNextFileNumber: payload_size = sizeof(uint64_t);
                break;
            case ManifestOp::AddSST: payload_size = sizeof(uint64_t) + sizeof(uint32_t);
                break;
            case ManifestOp::DelSST: payload_size = sizeof(uint64_t);
                break;
            case ManifestOp::AddWAL: payload_size = sizeof(uint64_t);
                break;
            case ManifestOp::DelWAL: payload_size = sizeof(uint64_t);
                break;
            default: return false;
        }

        constexpr uint32_t magic = kManifestMagic;
        constexpr uint32_t version = kManifestVersion;
        const auto op_u8 = static_cast<uint8_t>(op);

        ofs.write(reinterpret_cast<const char *>(&magic), sizeof(magic));
        ofs.write(reinterpret_cast<const char *>(&version), sizeof(version));
        ofs.write(reinterpret_cast<const char *>(&op_u8), sizeof(op_u8));
        ofs.write(reinterpret_cast<const char *>(&payload_size), sizeof(payload_size));

        switch (op) {
            case ManifestOp::SetNextFileNumber:
            case ManifestOp::DelSST:
            case ManifestOp::AddWAL:
            case ManifestOp::DelWAL:
                ofs.write(reinterpret_cast<const char *>(&id), sizeof(id));
                break;
            case ManifestOp::AddSST:
                ofs.write(reinterpret_cast<const char *>(&id), sizeof(id));
                ofs.write(reinterpret_cast<const char *>(&level), sizeof(level));
                break;
            default:
                return false;
        }

        ofs.flush();
        if (!ofs) {
            LOG_ERROR("Failed to write MANIFEST.log content");
            return false;
        }
        ofs.close();
    } else {
        LOG_ERROR("Failed to open MANIFEST.log for appending");
        return false;
    }
    return true;
}

/*
 * 读取MANIFEST.log
 * IO + 解析 + 顺序驱动
 */
bool ManifestManager::ReplayLog(ManifestState &state) const {
    LOG_INFO("Replay MANIFEST.log start.");
    if (!fs::exists(db_path_) || !fs::is_directory(db_path_)) {
        LOG_ERROR("DB Path error: " + db_path_);
        return false;
    }

    const fs::path p = fs::path(db_path_) / "MANIFEST.log";

    if (!fs::exists(p)) {
        return true;
    }

    if (std::ifstream ifs(p, std::ios::binary); ifs) {
        while (true) {
            uint32_t magic = 0;
            if (!ifs.read(reinterpret_cast<char *>(&magic), sizeof(magic))) {
                break;
            }

            if (magic != kManifestMagic) {
                LOG_ERROR("Manifest record magic mismatch, possible corruption. Stopping replay.");
                return false;
            }

            uint32_t version = 0;
            uint8_t op_raw = 0;
            uint32_t log_payload_size = 0;

            if (!ifs.read(reinterpret_cast<char *>(&version), sizeof(version)) ||
                !ifs.read(reinterpret_cast<char *>(&op_raw), sizeof(op_raw)) ||
                !ifs.read(reinterpret_cast<char *>(&log_payload_size), sizeof(log_payload_size))) {
                LOG_WARN("Truncated manifest header detected at end of file. Stopping.");
                break;
            }

            const auto op = static_cast<ManifestOp>(op_raw);
            if (op < ManifestOp::SetNextFileNumber || op > ManifestOp::DelWAL) {
                LOG_ERROR("Unknown ManifestOp");
                return false;
            }

            uint32_t payload_size = 0;
            switch (op) {
                case ManifestOp::SetNextFileNumber: payload_size = sizeof(uint64_t);
                    break;
                case ManifestOp::AddSST: payload_size = sizeof(uint64_t) + sizeof(uint32_t);
                    break;
                case ManifestOp::DelSST: payload_size = sizeof(uint64_t);
                    break;
                case ManifestOp::AddWAL: payload_size = sizeof(uint64_t);
                    break;
                case ManifestOp::DelWAL: payload_size = sizeof(uint64_t);
                    break;
                default: return false;
            }

            if (log_payload_size != payload_size) {
                LOG_ERROR("Payload size error");
                return false;
            }

            if (version != kManifestVersion) {
                LOG_ERROR("Manifest version mismatch");
                return false;
            }

            uint64_t id = 0;
            uint32_t level = 0;
            bool read_success = false;

            if (op == ManifestOp::AddSST) {
                read_success = (ifs.read(reinterpret_cast<char *>(&id), sizeof(id)) &&
                    ifs.read(reinterpret_cast<char *>(&level), sizeof(level)));
            } else {
                read_success = !!ifs.read(reinterpret_cast<char *>(&id), sizeof(id));
            }

            if (!read_success) {
                LOG_WARN("Truncated manifest payload detected. Partial record ignored.");
                break;
            }

            if (!ApplyEdit(state, op, id, level)) {
                LOG_ERROR("Failed to apply manifest edit");
                return false;
            }
        }
    } else {
        LOG_ERROR("MANIFEST.log exists but cannot be opened");
        return false;
    }
    return true;
}

/*
 * 纯状态变更
 * 根据 op 改 manifest_state_
 * payload:
        SetNextFileNumber: u64 next_file_number
        AddSST: u64 file_number + u32 level
        DelSST: u64 file_number
        AddWAL/DelWAL: u64 wal_id
 */
bool ManifestManager::ApplyEdit(ManifestState &state, const ManifestOp op, const uint64_t id, const uint32_t level) {
    switch (op) {
        case ManifestOp::SetNextFileNumber:
            state.next_file_number = id;
            break;
        case ManifestOp::DelSST:
            state.sst_levels.erase(id);
            break;
        case ManifestOp::AddWAL:
            state.live_wals.insert(id);
            break;
        case ManifestOp::DelWAL:
            state.live_wals.erase(id);
            break;
        case ManifestOp::AddSST:
            state.sst_levels[id] = level;
            break;
        default:
            return false;
    }
    return true;
}

/*
 * 日志主写 + 失败回退
 */
void ManifestManager::RecordEdit(ManifestState &state,
                                 uint32_t &edits_since_checkpoint,
                                 const ManifestOp op,
                                 const uint64_t id,
                                 const uint32_t level) const {
    if (!AppendEdit(op, id, level)) {
        LOG_ERROR("append manifest edit failed, fallback to snapshot");
        if (!PersistState(state)) {
            LOG_ERROR("fallback snapshot failed");
        }
        return;
    }
    ++edits_since_checkpoint;
    MaybeCheckpoint(state, edits_since_checkpoint);
}

/*
 * 检查更新 MANIFEST.log 到 MANIFEST 快照
 */
void ManifestManager::MaybeCheckpoint(ManifestState &state, uint32_t &edits_since_checkpoint) const {
    if (edits_since_checkpoint < kManifestCheckpointThreshold) return;

    if (!PersistState(state)) {
        LOG_ERROR("checkpoint snapshot failed, keep MANIFEST.log");
        return;
    }

    if (!TruncateLog()) {
        LOG_ERROR("checkpoint truncate failed after snapshot");
        return;
    }

    edits_since_checkpoint = 0;
    LOG_INFO("Manifest checkpoint completed");
}

/*
 * 暂时作用: 清空MANIFEST.log
 */
bool ManifestManager::TruncateLog() const {
    if (!fs::exists(db_path_) || !fs::is_directory(db_path_)) {
        LOG_ERROR("DB Path error: " + db_path_);
        return false;
    }
    const fs::path p = fs::path(db_path_) / "MANIFEST.log";
    if (!fs::exists(p)) {
        LOG_ERROR("MANIFEST.log does not exist");
        return false;
    }
    if (const std::ofstream ofs(p, std::ios::trunc); ofs) {
        return true;
    }
    return false;
}

bool ManifestManager::LoadState(ManifestState &state) const {
    LOG_INFO("Load manifest state start.");
    if (!fs::exists(db_path_) || !fs::is_directory(db_path_)) {
        LOG_ERROR("DB Path error: " + db_path_);
        return false;
    }

    const fs::path p = fs::path(db_path_) / "MANIFEST";

    if (!fs::exists(p)) {
        return false;
    }

    if (std::ifstream ifs(p, std::ios::binary); ifs) {
        uint32_t magic = 0;
        uint32_t version = 0;
        uint64_t next_file_number = 0;
        uint32_t sst_count = 0;
        uint32_t wal_count = 0;

        if (!ifs.read(reinterpret_cast<char *>(&magic), sizeof(magic))) return false;
        if (magic != kManifestMagic) return false;
        if (!ifs.read(reinterpret_cast<char *>(&version), sizeof(version))) return false;
        if (version != kManifestVersion) return false;
        if (!ifs.read(reinterpret_cast<char *>(&next_file_number), sizeof(next_file_number))) return false;
        if (!ifs.read(reinterpret_cast<char *>(&sst_count), sizeof(sst_count))) return false;

        state.next_file_number = next_file_number;
        state.sst_levels.clear();
        for (uint32_t i = 0; i < sst_count; ++i) {
            uint64_t file_number = 0;
            uint32_t level = 0;
            if (!ifs.read(reinterpret_cast<char *>(&file_number), sizeof(file_number))) return false;
            if (!ifs.read(reinterpret_cast<char *>(&level), sizeof(level))) return false;
            state.sst_levels[file_number] = level;
        }

        state.live_wals.clear();
        if (!ifs.read(reinterpret_cast<char *>(&wal_count), sizeof(wal_count))) return false;
        for (uint32_t i = 0; i < wal_count; ++i) {
            uint64_t wal_id = 0;
            if (!ifs.read(reinterpret_cast<char *>(&wal_id), sizeof(wal_id))) return false;
            state.live_wals.insert(wal_id);
        }
    } else {
        LOG_INFO("Manifest file can't be opened.");
        return false;
    }
    LOG_INFO("Load manifest state completed.");
    return true;
}

bool ManifestManager::PersistState(const ManifestState &state) const {
    if (!fs::exists(db_path_) || !fs::is_directory(db_path_)) {
        LOG_ERROR("DB Path error: " + db_path_);
        return false;
    }

    const fs::path final_path = fs::path(db_path_) / "MANIFEST";
    const fs::path tmp_path = fs::path(db_path_) / "MANIFEST.tmp";

    if (std::ofstream ofs(tmp_path, std::ios::binary | std::ios::trunc); ofs) {
        ofs.write(reinterpret_cast<const char *>(&kManifestMagic), sizeof(kManifestMagic));
        ofs.write(reinterpret_cast<const char *>(&kManifestVersion), sizeof(kManifestVersion));
        ofs.write(reinterpret_cast<const char *>(&state.next_file_number), sizeof(state.next_file_number));

        const auto sst_count = static_cast<uint32_t>(state.sst_levels.size());
        ofs.write(reinterpret_cast<const char *>(&sst_count), sizeof(sst_count));

        std::vector<std::pair<uint64_t, uint32_t> > sst_entries(
            state.sst_levels.begin(),
            state.sst_levels.end());
        std::sort(sst_entries.begin(),
                  sst_entries.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });

        for (const auto &[file_number, level] : sst_entries) {
            ofs.write(reinterpret_cast<const char *>(&file_number), sizeof(file_number));
            ofs.write(reinterpret_cast<const char *>(&level), sizeof(level));
        }

        const auto wal_count = static_cast<uint32_t>(state.live_wals.size());
        ofs.write(reinterpret_cast<const char *>(&wal_count), sizeof(wal_count));
        for (const uint64_t wal_id : state.live_wals) {
            ofs.write(reinterpret_cast<const char *>(&wal_id), sizeof(wal_id));
        }

        ofs.flush();
        if (!ofs) {
            LOG_ERROR("Failed to write MANIFEST content");
            return false;
        }

        ofs.close();
    } else {
        LOG_ERROR("Failed to open MANIFEST.tmp for writing");
        return false;
    }

    fs::rename(tmp_path, final_path);
    return true;
}
