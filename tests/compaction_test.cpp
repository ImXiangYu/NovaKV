//
// Created by 26708 on 2026/2/10.
//

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <algorithm>

#include "DBImpl.h"

namespace fs = std::filesystem;

namespace {
void PutValue(DBImpl& db, const std::string& key, const std::string& value) {
    ValueRecord record{ValueType::kValue, value};
    db.Put(key, record);
}

void PutDeletion(DBImpl& db, const std::string& key) {
    ValueRecord record{ValueType::kDeletion, ""};
    db.Put(key, record);
}

bool GetValue(DBImpl& db, const std::string& key, std::string& value) {
    ValueRecord record{ValueType::kValue, ""};
    if (!db.Get(key, record)) {
        return false;
    }
    if (record.type == ValueType::kDeletion) {
        return false;
    }
    value = record.value;
    return true;
}

int MaxDataFileNumber(const std::string& dir) {
    int max_id = -1;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto ext = entry.path().extension().string();
        if (ext != ".wal" && ext != ".sst") {
            continue;
        }
        const std::string stem = entry.path().stem().string();
        if (stem.empty() || !std::all_of(stem.begin(), stem.end(), [](unsigned char c) { return std::isdigit(c); })) {
            continue;
        }
        max_id = std::max(max_id, std::stoi(stem));
    }
    return max_id;
}
} // namespace

class CompactionTest : public ::testing::Test {
protected:
    std::string test_db_path = "./test_compact_db";

    void SetUp() override {
        if (fs::exists(test_db_path)) {
            fs::remove_all(test_db_path);
        }
        fs::create_directories(test_db_path);
    }

    void TearDown() override {
        if (fs::exists(test_db_path)) {
            fs::remove_all(test_db_path);
        }
    }
};

TEST_F(CompactionTest, ManualL0ToL1CompactionKeepsNewestValue) {
    DBImpl db(test_db_path);

    PutValue(db, "dup", "old");
    for (int i = 0; i < 999; ++i) {
        PutValue(db, "k1_" + std::to_string(i), "v");
    }
    PutValue(db, "trigger_1", "x"); // 触发第一次 MinorCompaction

    PutValue(db, "dup", "new");
    for (int i = 0; i < 999; ++i) {
        PutValue(db, "k2_" + std::to_string(i), "v");
    }
    PutValue(db, "trigger_2", "y"); // 触发第二次 MinorCompaction

    db.CompactL0ToL1();

    EXPECT_EQ(db.LevelSize(0), 0u);
    EXPECT_EQ(db.LevelSize(1), 1u);

    std::string val;
    EXPECT_TRUE(GetValue(db, "dup", val));
    EXPECT_EQ(val, "new");
}

TEST_F(CompactionTest, AutoL0ToL1CompactionTriggeredOnThreshold) {
    DBImpl db(test_db_path);

    PutValue(db, "dup", "old");
    for (int i = 0; i < 999; ++i) {
        PutValue(db, "k1_" + std::to_string(i), "v");
    }
    PutValue(db, "trigger_1", "x"); // 触发第一次 MinorCompaction

    PutValue(db, "dup", "new");
    for (int i = 0; i < 999; ++i) {
        PutValue(db, "k2_" + std::to_string(i), "v");
    }
    PutValue(db, "trigger_2", "y"); // 触发第二次 MinorCompaction

    EXPECT_EQ(db.LevelSize(0), 0u);
    EXPECT_EQ(db.LevelSize(1), 1u);

    std::string val;
    EXPECT_TRUE(GetValue(db, "dup", val));
    EXPECT_EQ(val, "new");
}

TEST_F(CompactionTest, RecoverSSTablesOnStartup) {
    {
        DBImpl db(test_db_path);
        for (int i = 0; i < 1000; ++i) {
            PutValue(db, "k_" + std::to_string(i), "v_" + std::to_string(i));
        }
        PutValue(db, "trigger", "x");
    }

    DBImpl db_recovered(test_db_path);
    std::string val;
    EXPECT_TRUE(GetValue(db_recovered, "k_10", val));
    EXPECT_EQ(val, "v_10");
}

TEST_F(CompactionTest, DestructorCleansUpReaders) {
    {
        DBImpl db(test_db_path);
        for (int i = 0; i < 1000; ++i) {
            PutValue(db, "k_" + std::to_string(i), "v_" + std::to_string(i));
        }
        PutValue(db, "trigger", "x"); // 触发一次落盘，确保有 SSTable reader
    }

    SUCCEED();
}

TEST_F(CompactionTest, NextFileNumberMonotonicAcrossRestart) {
    {
        DBImpl db(test_db_path);
        for (int i = 0; i < 1200; ++i) {
            PutValue(db, "first_" + std::to_string(i), "v");
        }
    }

    const int max_id_before_restart = MaxDataFileNumber(test_db_path);
    ASSERT_GE(max_id_before_restart, 0);

    {
        DBImpl db(test_db_path);
        for (int i = 0; i < 1200; ++i) {
            PutValue(db, "second_" + std::to_string(i), "v");
        }
    }

    const int max_id_after_restart = MaxDataFileNumber(test_db_path);
    EXPECT_GT(max_id_after_restart, max_id_before_restart);
}

// Test Intent:
// 当 L0->L1 compaction 的输入全是“可安全清理”的 tombstone（L1 不存在旧 value）时，
// 不应在 L1 产生新的 SST，也不应在磁盘残留新的空 SST 文件。
TEST_F(CompactionTest, CompactDropsBottomMostTombstonesWithoutCreatingNewSST) {
    DBImpl db(test_db_path);

    // 构造一个仅包含 tombstone 的 L0 SST：
    // 第 1001 次写入前会触发一次 MinorCompaction，前 1000 条 tombstone 落入 L0。
    for (int i = 0; i <= 1000; ++i) {
        PutDeletion(db, "ghost_" + std::to_string(i));
    }

    ASSERT_EQ(db.LevelSize(0), 1u);
    ASSERT_EQ(db.LevelSize(1), 0u);

    const int max_id_before = MaxDataFileNumber(test_db_path);
    ASSERT_GE(max_id_before, 0);

    db.CompactL0ToL1();

    EXPECT_EQ(db.LevelSize(0), 0u);
    EXPECT_EQ(db.LevelSize(1), 0u);

    const int max_id_after = MaxDataFileNumber(test_db_path);
    EXPECT_EQ(max_id_after, max_id_before);

    std::string val;
    EXPECT_FALSE(GetValue(db, "ghost_10", val));
}
