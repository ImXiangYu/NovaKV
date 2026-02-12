#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <string>
#include <filesystem>
#include "MemTable.h"

// 定义一个基础 Fixture，统一管理文件清理逻辑
class MemTableBaseTest : public ::testing::Test {
protected:
    // 每个 TEST_F 都会使用独立的文件名，防止并发运行或残留干扰
    const std::string basic_log = "temp_basic.log";
    const std::string persistence_log = "test_persistence.log";

    // 每次测试开始前，暴力清理掉这两个可能存在的文件
    void SetUp() override {
        std::filesystem::remove(basic_log);
        std::filesystem::remove(persistence_log);
    }

    // 每次测试结束后，清理现场，不留垃圾文件
    void TearDown() override {
        std::filesystem::remove(basic_log);
        std::filesystem::remove(persistence_log);
    }
};

// --- 第一部分：逻辑与并发测试 (使用 basic_log) ---

TEST_F(MemTableBaseTest, BasicOperations) {
    MemTable<int, std::string> mt(basic_log);
    mt.Put(1, "apple");
    mt.Put(2, "banana");

    std::string val;
    EXPECT_TRUE(mt.Get(1, val));
    EXPECT_EQ(val, "apple");

    mt.Put(1, "cherry");
    EXPECT_TRUE(mt.Get(1, val));
    EXPECT_EQ(val, "cherry");

    EXPECT_TRUE(mt.Remove(2));
    EXPECT_FALSE(mt.Get(2, val));
    EXPECT_EQ(mt.Count(), 1);
}

TEST_F(MemTableBaseTest, BulkInsert) {
    MemTable<int, int> mt(basic_log);
    int n = 10000;
    for (int i = 0; i < n; ++i) {
        mt.Put(i, i * 2);
    }
    EXPECT_EQ(mt.Count(), n);
}

TEST_F(MemTableBaseTest, ConcurrentReadWrite) {
    MemTable<int, int> mt(basic_log);
    const int num_writers = 4;
    const int ops_per_thread = 2000;
    std::vector<std::thread> workers;

    for (int i = 0; i < num_writers; ++i) {
        workers.emplace_back([&mt, i, ops_per_thread]() {
            for (int j = 0; j < ops_per_thread; ++j) {
                mt.Put(i * ops_per_thread + j, j);
            }
        });
    }

    for (auto& t : workers) t.join();
    EXPECT_EQ(mt.Count(), num_writers * ops_per_thread);
}

// --- 第二部分：恢复功能测试 (使用 persistence_log) ---

TEST_F(MemTableBaseTest, NoAutoRecoveryOnReopen) {
    {
        MemTable<int, std::string> mt(persistence_log);
        mt.Put(1, "apple");
        mt.Put(2, "banana");
        mt.Remove(1);
    } // mt 析构

    MemTable<int, std::string> mt_new(persistence_log);
    std::string val;
    // 恢复职责已经上移到 DBImpl，MemTable 构造不再自动回放 WAL。
    EXPECT_EQ(mt_new.Count(), 0);
    EXPECT_FALSE(mt_new.Get(1, val));
    EXPECT_FALSE(mt_new.Get(2, val));
}

TEST_F(MemTableBaseTest, ManualReplayCanRecoverData) {
    const int count = 1000;
    {
        MemTable<int, int> mt(persistence_log);
        for (int i = 0; i < count; ++i) {
            mt.Put(i, i * 10);
        }
    } // 模拟崩溃

    MemTable<int, int> mt_recovery(persistence_log);
    EXPECT_EQ(mt_recovery.Count(), 0);

    WalHandler wal(persistence_log);
    wal.LoadLog([&mt_recovery](ValueType type, const std::string& k, const std::string& v) {
        if (type == ValueType::kValue) {
            int key;
            int value;
            std::memcpy(&key, k.data(), sizeof(int));
            std::memcpy(&value, v.data(), sizeof(int));
            mt_recovery.ApplyWithoutWal(key, value);
        }
    });

    EXPECT_EQ(mt_recovery.Count(), count);
    int val;
    EXPECT_TRUE(mt_recovery.Get(500, val));
    EXPECT_EQ(val, 5000);
}
