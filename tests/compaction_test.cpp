//
// Created by 26708 on 2026/2/10.
//

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "DBImpl.h"

namespace fs = std::filesystem;

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

    db.Put("dup", "old");
    for (int i = 0; i < 999; ++i) {
        db.Put("k1_" + std::to_string(i), "v");
    }
    db.Put("trigger_1", "x"); // 触发第一次 MinorCompaction

    db.Put("dup", "new");
    for (int i = 0; i < 999; ++i) {
        db.Put("k2_" + std::to_string(i), "v");
    }
    db.Put("trigger_2", "y"); // 触发第二次 MinorCompaction

    db.CompactL0ToL1();

    EXPECT_EQ(db.LevelSize(0), 0u);
    EXPECT_EQ(db.LevelSize(1), 1u);

    std::string val;
    EXPECT_TRUE(db.Get("dup", val));
    EXPECT_EQ(val, "new");
}

TEST_F(CompactionTest, AutoL0ToL1CompactionTriggeredOnThreshold) {
    DBImpl db(test_db_path);

    db.Put("dup", "old");
    for (int i = 0; i < 999; ++i) {
        db.Put("k1_" + std::to_string(i), "v");
    }
    db.Put("trigger_1", "x"); // 触发第一次 MinorCompaction

    db.Put("dup", "new");
    for (int i = 0; i < 999; ++i) {
        db.Put("k2_" + std::to_string(i), "v");
    }
    db.Put("trigger_2", "y"); // 触发第二次 MinorCompaction

    EXPECT_EQ(db.LevelSize(0), 0u);
    EXPECT_EQ(db.LevelSize(1), 1u);

    std::string val;
    EXPECT_TRUE(db.Get("dup", val));
    EXPECT_EQ(val, "new");
}
