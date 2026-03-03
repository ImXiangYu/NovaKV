//
// Created by 26708 on 2026/2/10.
//

#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <algorithm>
#include <cctype>
#include <vector>

#include "DBImpl.h"

namespace fs = std::filesystem;

namespace {
// 辅助函数：向 DB 写入普通数据
void PutValue(DBImpl& db, const std::string& key, const std::string& value) {
    ValueRecord record{ValueType::kValue, value};
    db.Put(key, record);
}

// 辅助函数：向 DB 写入删除标记 (Tombstone)
void PutDeletion(DBImpl& db, const std::string& key) {
    ValueRecord record{ValueType::kDeletion, ""};
    db.Put(key, record);
}

// 辅助函数：从 DB 读取数据并校验类型
bool GetValue(DBImpl& db, const std::string& key, std::string& value) {
    ValueRecord record{ValueType::kValue, ""};
    if (!db.Get(key, record)) {
        return false;
    }
    // DBImpl::Get 内部已经处理了 kDeletion 返回 false 的逻辑
    // 这里保持辅助函数语义一致
    value = record.value;
    return true;
}

// 辅助函数：获取目录下数字命名的最大文件编号 (SST 或 WAL)
uint64_t GetMaxFileNumberOnDisk(const std::string& dir) {
    uint64_t max_id = 0;
    if (!fs::exists(dir)) return 0;
    
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        
        auto ext = entry.path().extension().string();
        if (ext != ".wal" && ext != ".sst") continue;
        
        std::string stem = entry.path().stem().string();
        if (stem.empty() || !std::all_of(stem.begin(), stem.end(), [](unsigned char c) { return std::isdigit(c); })) {
            continue;
        }
        max_id = std::max(max_id, std::stoull(stem));
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

// Test Intent: 验证 L0->L1 压缩时，多个版本的同名 Key 能够正确合并，保留最新值。
TEST_F(CompactionTest, L0ToL1CompactionKeepsNewestValue) {
    DBImpl db(test_db_path);

    // 第一批数据：L0 SST 1
    PutValue(db, "dup", "old");
    for (int i = 0; i < 999; ++i) {
        PutValue(db, "k1_" + std::to_string(i), "v");
    }
    PutValue(db, "trigger_1", "x"); // 触发第一次 MinorCompaction，生成一个 L0 SST

    // 第二批数据：L0 SST 2
    PutValue(db, "dup", "new");
    for (int i = 0; i < 999; ++i) {
        PutValue(db, "k2_" + std::to_string(i), "v");
    }
    // 触发第二次 MinorCompaction，由于 L0 数量达到阈值 (>=2)，会随后自动触发 L0->L1
    PutValue(db, "trigger_2", "y"); 

    // 验证层级状态
    EXPECT_EQ(db.LevelSize(0), 0u);
    EXPECT_EQ(db.LevelSize(1), 1u);

    // 验证值正确性
    std::string val;
    EXPECT_TRUE(GetValue(db, "dup", val));
    EXPECT_EQ(val, "new");
}

// Test Intent: 验证数据库重启后，已持久化的 SSTable 及其所属层级（L1）能被正确加载。
TEST_F(CompactionTest, RecoverSSTablesWithLevelsOnStartup) {
    {
        DBImpl db(test_db_path);
        // 构造两次落盘，触发 L0->L1
        for (int i = 0; i < 1000; ++i) PutValue(db, "r1_" + std::to_string(i), "v");
        PutValue(db, "t1", "x"); 
        for (int i = 0; i < 1000; ++i) PutValue(db, "r2_" + std::to_string(i), "v");
        PutValue(db, "t2", "y"); 
        
        ASSERT_EQ(db.LevelSize(1), 1u);
    }

    // 重启
    DBImpl db_recovered(test_db_path);
    EXPECT_EQ(db_recovered.LevelSize(1), 1u);
    
    std::string val;
    EXPECT_TRUE(GetValue(db_recovered, "r1_10", val));
}

// Test Intent: 验证析构函数能够安全清理资源，无崩溃。
TEST_F(CompactionTest, DestructorSafety) {
    {
        DBImpl db(test_db_path);
        PutValue(db, "quick", "data");
    }
    SUCCEED();
}

// Test Intent: 验证数据库重启后，新分配的文件编号能够接续之前的最大编号，避免文件冲突。
TEST_F(CompactionTest, NextFileNumberIsMonotonicAfterRestart) {
    uint64_t max_id_before = 0;
    {
        DBImpl db(test_db_path);
        for (int i = 0; i < 1200; ++i) {
            PutValue(db, "first_" + std::to_string(i), "v");
        }
        max_id_before = GetMaxFileNumberOnDisk(test_db_path);
    }

    ASSERT_GT(max_id_before, 0u);

    {
        DBImpl db(test_db_path);
        PutValue(db, "second", "data");
        // 构造函数会分配新的 WAL，Put 会写入 WAL，析构会落盘
    }

    uint64_t max_id_after = GetMaxFileNumberOnDisk(test_db_path);
    EXPECT_GT(max_id_after, max_id_before);
}

// Test Intent: 验证“底部清理”逻辑。
// 当 L0->L1 compaction 的输入全是 tombstone 且 L1 不存在对应旧值时，
// 压缩引擎应直接丢弃这些 tombstone，不产生空的 L1 SST 文件，不增加磁盘文件数。
TEST_F(CompactionTest, CompactDropsBottomMostTombstonesWithoutCreatingNewSST) {
    DBImpl db(test_db_path);

    // 1. 构造一个仅包含 tombstone 的 L0 SST (1001条数据触发 Minor)
    for (int i = 0; i <= 1000; ++i) {
        PutDeletion(db, "ghost_" + std::to_string(i));
    }

    ASSERT_EQ(db.LevelSize(0), 1u);
    ASSERT_EQ(db.LevelSize(1), 0u);

    uint64_t max_id_before = GetMaxFileNumberOnDisk(test_db_path);

    // 2. 手动触发 L0->L1 压缩（或写入更多数据自动触发）
    db.CompactL0ToL1();

    // 验证：L0 已清理，L1 也没有增加（因为全是无效 tombstone）
    EXPECT_EQ(db.LevelSize(0), 0u);
    EXPECT_EQ(db.LevelSize(1), 0u);

    // 验证：磁盘上没有产生新的编号更大的 SST 文件
    uint64_t max_id_after = GetMaxFileNumberOnDisk(test_db_path);
    EXPECT_EQ(max_id_after, max_id_before);

    std::string val;
    EXPECT_FALSE(GetValue(db, "ghost_10", val));
}
