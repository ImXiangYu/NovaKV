//
// Created by 26708 on 2026/2/5.
//

#include <gtest/gtest.h>
#include "BlockBuilder.h"

class BlockBuilderTest : public ::testing::Test {
    protected:
        // 每次测试前都会执行 SetUp
        void SetUp() override {
            builder_ = new BlockBuilder();
        }

        // 每次测试后执行 TearDown
        void TearDown() override {
            delete builder_;
        }

        BlockBuilder* builder_{};
};

// 测试 1：验证初始状态是否为空
TEST_F(BlockBuilderTest, EmptyInitially) {
    EXPECT_TRUE(builder_->Empty());
    EXPECT_EQ(builder_->CurrentSizeEstimate(), 0);
}

// 测试 2：验证单条记录写入后的长度计算
// 布局：KeyLen(4) + "key1"(4) + ValLen(4) + "value1"(6) = 18 字节
TEST_F(BlockBuilderTest, AddSingleEntry) {
    builder_->Add("key1", "value1");

    EXPECT_FALSE(builder_->Empty());
    // 4 + 4 + 4 + 6 = 18
    EXPECT_EQ(builder_->CurrentSizeEstimate(), 18);
}

// 测试 3 : 验证多条记录连续写入
TEST_F(BlockBuilderTest, AddMultipleEntries) {
    builder_->Add("k1", "v1"); // 4+2 + 4+2 = 12
    builder_->Add("k2", "v2"); // 12 + 12 = 24

    EXPECT_EQ(builder_->CurrentSizeEstimate(), 24);
}

// 测试 4：验证 Reset 功能是否清空数据
TEST_F(BlockBuilderTest, ResetLogic) {
    builder_->Add("test", "data");
    builder_->Reset();

    EXPECT_TRUE(builder_->Empty());
    EXPECT_EQ(builder_->CurrentSizeEstimate(), 0);
}

// 测试 5：验证 Finish 后的数据完整性（可选，简单校验）
TEST_F(BlockBuilderTest, FinishReturnsData) {
    std::string k = "hi";
    std::string v = "world";
    builder_->Add(k, v);

    std::string result = builder_->Finish();
    EXPECT_EQ(result.size(), 4 + 2 + 4 + 5);
    // 验证前 4 个字节是否记录了长度 2
    uint32_t len;
    memcpy(&len, result.data(), sizeof(uint32_t));
    EXPECT_EQ(len, 2);
}