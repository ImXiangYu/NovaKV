//
// Created by 26708 on 2026/2/4.
//

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <string>
#include "MemTable.h"

// 1. 基础功能测试：确保单线程下逻辑正确
TEST(MemTableTest, BasicOperations) {
    MemTable<int, std::string> mt;

    // 测试插入与查询
    mt.Put(1, "apple");
    mt.Put(2, "banana");

    std::string val;
    EXPECT_TRUE(mt.Get(1, val));
    EXPECT_EQ(val, "apple");

    // 测试覆盖更新
    mt.Put(1, "cherry");
    EXPECT_TRUE(mt.Get(1, val));
    EXPECT_EQ(val, "cherry");

    // 测试删除
    EXPECT_TRUE(mt.Remove(2));
    EXPECT_FALSE(mt.Get(2, val));
    EXPECT_EQ(mt.Count(), 1);
}

// 2. 边界条件测试
TEST(MemTableTest, EdgeCases) {
    MemTable<int, int> mt;
    int val;

    // 查询不存在的键
    EXPECT_FALSE(mt.Get(999, val));

    // 删除不存在的键
    EXPECT_FALSE(mt.Remove(999));

    // 插入负数或特殊值（如果你的 Key 类型支持）
    mt.Put(-10, 100);
    EXPECT_TRUE(mt.Get(-10, val));
    EXPECT_EQ(val, 100);
}

// 3. 压力测试：大批量顺序/随机插入
TEST(MemTableTest, BulkInsert) {
    MemTable<int, int> mt;
    int n = 10000;
    for (int i = 0; i < n; ++i) {
        mt.Put(i, i * 2);
    }
    EXPECT_EQ(mt.Count(), n);

    int val;
    for (int i = 0; i < n; i += 100) { // 抽样检查
        EXPECT_TRUE(mt.Get(i, val));
        EXPECT_EQ(val, i * 2);
    }
}

// 4. 核心：多线程并发读写测试
// 模拟 4 个线程写，8 个线程读，验证锁是否能有效防止 Crash 并保持数据一致性
TEST(MemTableTest, ConcurrentReadWrite) {
    MemTable<int, int> mt;
    const int num_writers = 4;
    const int num_readers = 8;
    const int ops_per_thread = 2000;

    std::vector<std::thread> workers;

    // 写线程：插入数据
    for (int i = 0; i < num_writers; ++i) {
        workers.emplace_back([&mt, i, ops_per_thread]() {
            for (int j = 0; j < ops_per_thread; ++j) {
                mt.Put(i * ops_per_thread + j, j);
            }
        });
    }

    // 读线程：尝试读取可能还没写入的数据
    for (int i = 0; i < num_readers; ++i) {
        workers.emplace_back([&mt, ops_per_thread, num_writers]() {
            int val;
            for (int j = 0; j < ops_per_thread * num_writers; ++j) {
                mt.Get(j, val); // 不管是否 Get 成功，只要不 Crash 就行
            }
        });
    }

    for (auto& t : workers) {
        t.join();
    }

    EXPECT_EQ(mt.Count(), num_writers * ops_per_thread);
}