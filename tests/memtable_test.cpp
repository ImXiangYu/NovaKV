#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "MemTable.h"

namespace {
ValueRecord MakeValue(const std::string& value) {
    return ValueRecord{ValueType::kValue, value};
}
} // namespace

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
    MemTable mt(basic_log);

    mt.Put("k1", MakeValue("apple"));
    mt.Put("k2", MakeValue("banana"));

    ValueRecord rec{ValueType::kDeletion, ""};
    EXPECT_TRUE(mt.Get("k1", rec));
    EXPECT_EQ(rec.type, ValueType::kValue);
    EXPECT_EQ(rec.value, "apple");

    mt.Put("k1", MakeValue("cherry"));
    EXPECT_TRUE(mt.Get("k1", rec));
    EXPECT_EQ(rec.type, ValueType::kValue);
    EXPECT_EQ(rec.value, "cherry");

    EXPECT_TRUE(mt.Remove("k2"));
    EXPECT_TRUE(mt.Get("k2", rec));
    EXPECT_EQ(rec.type, ValueType::kDeletion);
    EXPECT_TRUE(rec.value.empty());
    EXPECT_EQ(mt.Count(), 2);
}

TEST_F(MemTableBaseTest, BulkInsert) {
    MemTable mt(basic_log);
    const int n = 10000;
    for (int i = 0; i < n; ++i) {
        mt.Put("k_" + std::to_string(i), MakeValue("v_" + std::to_string(i)));
    }
    EXPECT_EQ(mt.Count(), n);
}

TEST_F(MemTableBaseTest, ConcurrentReadWrite) {
    MemTable mt(basic_log);
    const int num_writers = 4;
    const int ops_per_thread = 2000;
    std::vector<std::thread> workers;

    for (int i = 0; i < num_writers; ++i) {
        workers.emplace_back([&mt, i, ops_per_thread]() {
            for (int j = 0; j < ops_per_thread; ++j) {
                const std::string key = "k_" + std::to_string(i * ops_per_thread + j);
                mt.Put(key, MakeValue("v_" + std::to_string(j)));
            }
        });
    }

    for (auto& t : workers) {
        t.join();
    }
    EXPECT_EQ(mt.Count(), num_writers * ops_per_thread);
}

// --- 第二部分：恢复功能测试 (使用 persistence_log) ---

TEST_F(MemTableBaseTest, NoAutoRecoveryOnReopen) {
    {
        MemTable mt(persistence_log);
        mt.Put("a", MakeValue("apple"));
        mt.Put("b", MakeValue("banana"));
        mt.Remove("a");
    } // mt 析构

    MemTable mt_new(persistence_log);
    ValueRecord rec{ValueType::kDeletion, ""};
    // 恢复职责已经上移到 DBImpl，MemTable 构造不再自动回放 WAL。
    EXPECT_EQ(mt_new.Count(), 0);
    EXPECT_FALSE(mt_new.Get("a", rec));
    EXPECT_FALSE(mt_new.Get("b", rec));
}

TEST_F(MemTableBaseTest, ManualReplayCanRecoverDataAndTombstone) {
    const int count = 1000;
    {
        MemTable mt(persistence_log);
        for (int i = 0; i < count; ++i) {
            mt.Put("k_" + std::to_string(i), MakeValue("v_" + std::to_string(i)));
        }
        mt.Remove("k_500");
    } // 模拟崩溃

    MemTable mt_recovery(persistence_log);
    EXPECT_EQ(mt_recovery.Count(), 0);

    WalHandler wal(persistence_log);
    wal.LoadLog([&mt_recovery](ValueType type, const std::string& k, const std::string& v) {
        mt_recovery.ApplyWithoutWal(k, ValueRecord{type, v});
    });

    EXPECT_EQ(mt_recovery.Count(), count);

    ValueRecord rec{ValueType::kDeletion, ""};
    EXPECT_TRUE(mt_recovery.Get("k_10", rec));
    EXPECT_EQ(rec.type, ValueType::kValue);
    EXPECT_EQ(rec.value, "v_10");

    EXPECT_TRUE(mt_recovery.Get("k_500", rec));
    EXPECT_EQ(rec.type, ValueType::kDeletion);
    EXPECT_TRUE(rec.value.empty());
}
