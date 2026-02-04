#include <gtest/gtest.h>
#include "SkipList.h"
#include <climits>

// 测试插入和查询
TEST(SkipListBasic, InsertAndSearch) {
    SkipList<int, std::string> sl(8);
    sl.insert_element(1, "Value1");
    
    std::string result;
    ASSERT_TRUE(sl.search_element(1, result));
    EXPECT_EQ(result, "Value1");
}

// 测试删除
TEST(SkipListBasic, DeleteLogic) {
    SkipList<int, int> sl(8);
    sl.insert_element(10, 100);
    EXPECT_TRUE(sl.delete_element(10));
    
    int val;
    EXPECT_FALSE(sl.search_element(10, val));
}

// 测试空表
TEST(SkipListEdge, EmptyList) {
    SkipList<int, int> sl(8);
    int val;
    EXPECT_FALSE(sl.search_element(1, val));
    EXPECT_FALSE(sl.delete_element(1));
}

// 测试覆盖插入
TEST(SkipListLogic, DuplicateInsert) {
    SkipList<int, std::string> sl(8);
    sl.insert_element(1, "Original");
    sl.insert_element(1, "Updated"); // 再次插入相同的 Key

    std::string result;
    ASSERT_TRUE(sl.search_element(1, result));
    // 如果你的逻辑是覆盖，这里应该是 "Updated"
    EXPECT_EQ(result, "Updated"); 
}

// 测试删除不存在的元素
TEST(SkipListLogic, DeleteNonExistent) {
    SkipList<int, int> sl(8);
    EXPECT_FALSE(sl.delete_element(1)); // 删除一个不存在的元素
}

// 大规模压力测试
TEST(SkipListStress, MassiveData) {
    SkipList<int, int> sl(16);
    int count = 10000;

    // 批量插入
    for (int i = 0; i < count; ++i) {
        sl.insert_element(i, i * 2);
    }

    // 验证所有数据都能找回
    for (int i = 0; i < count; ++i) {
        int val;
        EXPECT_TRUE(sl.search_element(i, val));
        EXPECT_EQ(val, i * 2);
    }

    // 随机删除一半数据
    for (int i = 0; i < count; i += 2) {
        EXPECT_TRUE(sl.delete_element(i));
    }

    // 验证删除后的状态
    for (int i = 0; i < count; ++i) {
        int val;
        if (i % 2 == 0) EXPECT_FALSE(sl.search_element(i, val));
        else EXPECT_TRUE(sl.search_element(i, val));
    }
}

// 测试排序稳定性
TEST(SkipListLogic, OrderConsistency) {
    SkipList<int, int> sl(8);
    // 乱序插入
    sl.insert_element(5, 50);
    sl.insert_element(1, 10);
    sl.insert_element(10, 100);
    sl.insert_element(3, 30);

    // 验证能够按顺序查找到
    int val;
    EXPECT_TRUE(sl.search_element(1, val));
    EXPECT_TRUE(sl.search_element(3, val));
    EXPECT_TRUE(sl.search_element(5, val));
    EXPECT_TRUE(sl.search_element(10, val));
}

// 极端边界测试
TEST(SkipListEdge, BoundaryValues) {
    SkipList<int, int> sl(8);
    // 插入最小/最大整数
    sl.insert_element(INT_MIN, -1);
    sl.insert_element(INT_MAX, 1);

    int val;
    EXPECT_TRUE(sl.search_element(INT_MIN, val));
    EXPECT_TRUE(sl.search_element(INT_MAX, val));
    
    // 删除后再查找
    EXPECT_TRUE(sl.delete_element(INT_MIN));
    EXPECT_FALSE(sl.search_element(INT_MIN, val));
}

