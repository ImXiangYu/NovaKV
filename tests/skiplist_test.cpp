#include <gtest/gtest.h>
#include "SkipList.h"
#include <climits>
#include <string>

// 定义 Fixture 类
class SkipListTest : public ::testing::Test {
    protected:
        // 每个 TEST_F 开始前都会执行 SetUp
        void SetUp() override {
            sl = new SkipList<int, std::string>(8);
        }

        // 每个 TEST_F 结束后都会执行 TearDown
        void TearDown() override {
            delete sl;
        }

        SkipList<int, std::string>* sl;
};

// 1. 使用 Fixture 的基本测试
TEST_F(SkipListTest, InsertAndSearch) {
    sl->insert_element(1, "Value1");
    std::string result;
    ASSERT_TRUE(sl->search_element(1, result));
    EXPECT_EQ(result, "Value1");
}

// 2. 核心：验证迭代器的逻辑
TEST_F(SkipListTest, IteratorOrder) {
    // 乱序插入
    sl->insert_element(30, "thirty");
    sl->insert_element(10, "ten");
    sl->insert_element(20, "twenty");

    // 使用迭代器遍历
    auto it = sl->begin();

    // 第一条应该是 10 (有序性)
    ASSERT_TRUE(it.Valid());
    EXPECT_EQ(it.key(), 10);
    EXPECT_EQ(it.value(), "ten");

    // 移动到下一条
    it.Next();
    ASSERT_TRUE(it.Valid());
    EXPECT_EQ(it.key(), 20);
    EXPECT_EQ(it.value(), "twenty");

    // 移动到下一条
    it.Next();
    ASSERT_TRUE(it.Valid());
    EXPECT_EQ(it.key(), 30);
    EXPECT_EQ(it.value(), "thirty");

    // 再移动就该结束了
    it.Next();
    EXPECT_FALSE(it.Valid());
}

// 3. 验证迭代器处理空表
TEST_F(SkipListTest, EmptyListIterator) {
    auto it = sl->begin();
    EXPECT_FALSE(it.Valid());
}

// 4. 大规模压力测试 (复用 Fixture)
TEST_F(SkipListTest, MassiveDataWithIterator) {
    int count = 1000;
    for (int i = 0; i < count; ++i) {
        sl->insert_element(i, std::to_string(i));
    }

    // 通过迭代器验证顺序和完整性
    auto it = sl->begin();
    for (int i = 0; i < count; ++i) {
        ASSERT_TRUE(it.Valid());
        EXPECT_EQ(it.key(), i);
        it.Next();
    }
    EXPECT_FALSE(it.Valid());
}