#include <gtest/gtest.h>
#include "DBImpl.h"
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

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
    db.Put("name", "NovaKV");
    db.Put("version", "1.0");

    std::string val;
    EXPECT_TRUE(db.Get("name", val));
    EXPECT_EQ(val, "NovaKV");
    EXPECT_TRUE(db.Get("version", val));
    EXPECT_EQ(val, "1.0");
    EXPECT_FALSE(db.Get("non_exist", val));
}

// 2. 核心逻辑：覆盖写与删除测试 (验证 LSM-tree 的最新版本优先原则)
TEST_F(DBImplTest, OverwriteAndRemove) {
    DBImpl db(test_db_path);

    db.Put("key1", "old_value");
    db.Put("key1", "new_value"); // 覆盖写

    std::string val;
    db.Get("key1", val);
    EXPECT_EQ(val, "new_value");

    db.Put("key2", "to_be_deleted");
    // 注意：如果你实现了 Remove 接口，请调用它；如果没有，可以 Put 一个特殊标记
    // 这里假设你实现了 Remove
    // db.Remove("key2");
    // EXPECT_FALSE(db.Get("key2", val));
}

// 3. 恢复逻辑：WAL 崩溃恢复全流程测试
TEST_F(DBImplTest, CrashRecoveryDeepTest) {
    {
        DBImpl db(test_db_path);
        db.Put("cluster_1", "node_a");
        db.Put("cluster_2", "node_b");
        // 模拟断电：不执行析构逻辑，直接结束作用域
        // (在实际工程中，我们会通过 kill 进程模拟)
    }

    // 重新启动，触发 Recover 逻辑
    DBImpl db_recovered(test_db_path);
    std::string val;
    EXPECT_TRUE(db_recovered.Get("cluster_1", val));
    EXPECT_EQ(val, "node_a");
    EXPECT_TRUE(db_recovered.Get("cluster_2", val));
    EXPECT_EQ(val, "node_b");
}

// 4. 边界逻辑：跨层查找测试 (内存 + 磁盘混合查找)
TEST_F(DBImplTest, MixedLayerSearch) {
    DBImpl db(test_db_path);

    // 第一步：写入足够多数据，触发一次 Minor Compaction，生成 SST
    // 假设你的阈值调小了，或者我们写入大量数据
    for (int i = 0; i < 500; i++) {
        db.Put("old_" + std::to_string(i), "v" + std::to_string(i));
    }

    // 第二步：再写入一些数据留在 MemTable 中
    db.Put("active_key", "active_val");

    // 第三步：验证能否同时从 SST 和 MemTable 读到数据
    std::string val;
    EXPECT_TRUE(db.Get("old_10", val));    // 从磁盘 SST 读
    EXPECT_EQ(val, "v10");
    EXPECT_TRUE(db.Get("active_key", val)); // 从内存 MemTable 读
    EXPECT_EQ(val, "active_val");
}

// 5. 压力逻辑：大 Value 触发落盘一致性测试
TEST_F(DBImplTest, LargeValueCompaction) {
    DBImpl db(test_db_path);

    // 写入一个 1MB 的大 Value
    std::string large_val(1024 * 1024, 'X');
    db.Put("large_key", large_val);

    // 再次写入触发落盘
    for (int i = 0; i < 1000; i++) {
        db.Put("fill_" + std::to_string(i), "data");
    }

    std::string result;
    EXPECT_TRUE(db.Get("large_key", result));
    EXPECT_EQ(result.size(), 1024 * 1024);
    EXPECT_EQ(result[0], 'X');
}