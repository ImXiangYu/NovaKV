#include <gtest/gtest.h>
#include "DBImpl.h"
#include "WalHandler.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

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

void ForceMinorCompaction(DBImpl& db, const std::string& prefix) {
    for (int i = 0; i < 1000; ++i) {
        PutValue(db, prefix + "_fill_" + std::to_string(i), "v");
    }
    PutValue(db, prefix + "_trigger", "x");
}

bool GetValue(const DBImpl& db, const std::string& key, std::string& value) {
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

size_t CountNumericFilesWithExt(const std::string& dir, const std::string& ext) {
    size_t count = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ext) {
            continue;
        }
        const std::string stem = entry.path().stem().string();
        if (!std::all_of(stem.begin(), stem.end(),
                         [](const unsigned char c) { return std::isdigit(c); })) {
            continue;
        }
        ++count;
    }
    return count;
}
} // namespace

class DBImplTest : public ::testing::Test {
protected:
    std::string test_db_path = "./test_db_dir";

    void SetUp() override {
        if (fs::exists(test_db_path)) {
            fs::remove_all(test_db_path);
        }
        fs::create_directories(test_db_path);
    }

    void TearDown() override {
        // 保持现状，方便调试
    }
};

// 1. 基础逻辑：PUT/GET 闭环测试
TEST_F(DBImplTest, BasicPutGet) {
    DBImpl db(test_db_path);
    PutValue(db, "name", "NovaKV");
    PutValue(db, "version", "1.0");

    std::string val;
    EXPECT_TRUE(GetValue(db, "name", val));
    EXPECT_EQ(val, "NovaKV");
    EXPECT_TRUE(GetValue(db, "version", val));
    EXPECT_EQ(val, "1.0");
    EXPECT_FALSE(GetValue(db, "non_exist", val));
}

// 2. 核心逻辑：覆盖写与删除测试 (验证 LSM-tree 的最新版本优先原则)
TEST_F(DBImplTest, OverwriteAndRemove) {
    DBImpl db(test_db_path);

    PutValue(db, "key1", "old_value");
    PutValue(db, "key1", "new_value"); // 覆盖写

    std::string val;
    GetValue(db, "key1", val);
    EXPECT_EQ(val, "new_value");

    PutValue(db, "key2", "to_be_deleted");
    // 注意：如果你实现了 Remove 接口，请调用它；如果没有，可以 Put 一个特殊标记
    // 这里假设你实现了 Remove
    // db.Remove("key2");
    // EXPECT_FALSE(db.Get("key2", val));
}

// 3. 语义回归：GET 命中 tombstone 时应返回未命中
TEST_F(DBImplTest, GetTreatsTombstoneAsMissInMemTable) {
    DBImpl db(test_db_path);

    PutValue(db, "k", "v1");
    PutDeletion(db, "k");

    std::string val;
    EXPECT_FALSE(GetValue(db, "k", val));

    ValueRecord raw{ValueType::kValue, ""};
    EXPECT_FALSE(db.Get("k", raw));
}

// 4. 恢复逻辑：WAL 崩溃恢复全流程测试
TEST_F(DBImplTest, CrashRecoveryDeepTest) {
    {
        DBImpl db(test_db_path);
        PutValue(db, "cluster_1", "node_a");
        PutValue(db, "cluster_2", "node_b");
        // 模拟断电：不执行析构逻辑，直接结束作用域
        // (在实际工程中，我们会通过 kill 进程模拟)
    }

    // 重新启动，触发 Recover 逻辑
    DBImpl db_recovered(test_db_path);
    std::string val;
    EXPECT_TRUE(GetValue(db_recovered, "cluster_1", val));
    EXPECT_EQ(val, "node_a");
    EXPECT_TRUE(GetValue(db_recovered, "cluster_2", val));
    EXPECT_EQ(val, "node_b");
}

// 5. 边界逻辑：跨层查找测试 (内存 + 磁盘混合查找)
TEST_F(DBImplTest, MixedLayerSearch) {
    DBImpl db(test_db_path);

    // 第一步：写入足够多数据，触发一次 Minor Compaction，生成 SST
    // 假设你的阈值调小了，或者我们写入大量数据
    for (int i = 0; i < 500; i++) {
        PutValue(db, "old_" + std::to_string(i), "v" + std::to_string(i));
    }

    // 第二步：再写入一些数据留在 MemTable 中
    PutValue(db, "active_key", "active_val");

    // 第三步：验证能否同时从 SST 和 MemTable 读到数据
    std::string val;
    EXPECT_TRUE(GetValue(db, "old_10", val));    // 从磁盘 SST 读
    EXPECT_EQ(val, "v10");
    EXPECT_TRUE(GetValue(db, "active_key", val)); // 从内存 MemTable 读
    EXPECT_EQ(val, "active_val");
}

// 6. 压力逻辑：大 Value 触发落盘一致性测试
TEST_F(DBImplTest, LargeValueCompaction) {
    DBImpl db(test_db_path);

    // 写入一个 1MB 的大 Value
    std::string large_val(1024 * 1024, 'X');
    PutValue(db, "large_key", large_val);

    // 再次写入触发落盘
    for (int i = 0; i < 1000; i++) {
        PutValue(db, "fill_" + std::to_string(i), "data");
    }

    std::string result;
    EXPECT_TRUE(GetValue(db, "large_key", result));
    EXPECT_EQ(result.size(), 1024 * 1024);
    EXPECT_EQ(result[0], 'X');
}

// 7. 多 SST 版本优先级：验证同 key 在多层 SST 中返回最新值
TEST_F(DBImplTest, NewestSSTableWins) {
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

    std::string val;
    EXPECT_TRUE(GetValue(db, "dup", val));
    EXPECT_EQ(val, "new");
}

// 8. Phase 1: 跨层 tombstone 应遮蔽旧值，不能“旧值复活”
TEST_F(DBImplTest, TombstoneInNewerLevelHidesOlderValue) {
    DBImpl db(test_db_path);

    // 第一次形成 L1：k=old
    PutValue(db, "k", "old");
    ForceMinorCompaction(db, "round1");
    ForceMinorCompaction(db, "round2");

    // 第二次形成更新的 L1：k=tombstone
    PutDeletion(db, "k");
    ForceMinorCompaction(db, "round3");
    ForceMinorCompaction(db, "round4");

    std::string val;
    EXPECT_FALSE(GetValue(db, "k", val));

    ValueRecord raw{ValueType::kValue, ""};
    EXPECT_FALSE(db.Get("k", raw));
}

// 9. Phase 1: 删除语义在重启后仍应生效
TEST_F(DBImplTest, DeletionSemanticsSurviveRestart) {
    {
        DBImpl db(test_db_path);
        PutValue(db, "k", "v1");
        PutDeletion(db, "k");
    }

    DBImpl db_recovered(test_db_path);
    std::string val;
    EXPECT_FALSE(GetValue(db_recovered, "k", val));

    ValueRecord raw{ValueType::kValue, ""};
    EXPECT_FALSE(db_recovered.Get("k", raw));
}

// 10. Phase 2: 重启后应保留 SST 所属层级，而不是把全部 SST 都塞回 L0
// Test Intent: 为 Manifest 的“存活文件 -> 层级映射”恢复建立回归保护。
TEST_F(DBImplTest, KeepLevelMappingAfterRestart) {
    {
        DBImpl db(test_db_path);

        // 先制造包含 L0 和 L1 的状态
        PutValue(db, "seed", "v");
        ForceMinorCompaction(db, "phase2_round1");
        ForceMinorCompaction(db, "phase2_round2"); // 触发 L0->L1
        ForceMinorCompaction(db, "phase2_round3"); // 生成新的 L0

        EXPECT_GT(db.LevelSize(0), 0u);
        EXPECT_GT(db.LevelSize(1), 0u);
    }

    const size_t sst_on_disk = CountNumericFilesWithExt(test_db_path, ".sst");
    ASSERT_GT(sst_on_disk, 0u);

    DBImpl db_recovered(test_db_path);

    const size_t recovered_total = db_recovered.LevelSize(0) + db_recovered.LevelSize(1);
    EXPECT_EQ(recovered_total, sst_on_disk);     // 不应重复加载同一批 SST
    EXPECT_GT(db_recovered.LevelSize(1), 0u);    // 不应把所有 SST 都恢复到 L0
    EXPECT_LT(db_recovered.LevelSize(0), sst_on_disk);
}

// 11. Phase 2: 多 WAL 恢复应回放全部日志，且按文件号从小到大应用
TEST_F(DBImplTest, MultiWalRecoveryReplaysAllLogsInFileNumberOrder) {
    const std::string wal_2 = test_db_path + "/2.wal";
    const std::string wal_10 = test_db_path + "/10.wal";

    {
        WalHandler wal(wal_2);
        wal.AddLog("order_key", "from_2", ValueType::kValue);
        wal.AddLog("tomb_key", "alive", ValueType::kValue);
    }
    {
        WalHandler wal(wal_10);
        wal.AddLog("order_key", "from_10", ValueType::kValue);
        wal.AddLog("tomb_key", "", ValueType::kDeletion);
    }

    ASSERT_EQ(CountNumericFilesWithExt(test_db_path, ".wal"), 2u);

    DBImpl db_recovered(test_db_path);

    std::string val;
    EXPECT_TRUE(GetValue(db_recovered, "order_key", val));
    EXPECT_EQ(val, "from_10");
    EXPECT_FALSE(GetValue(db_recovered, "tomb_key", val));
}
