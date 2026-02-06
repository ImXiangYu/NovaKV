//
// Created by 26708 on 2026/2/6.
//

#include <gtest/gtest.h>
#include "SSTableBuilder.h"
#include "SSTableReader.h"
#include <filesystem>
#include <map>

class SSTableFullCycleTest : public ::testing::Test {
protected:
    const std::string test_file = "full_test.sst";

    void SetUp() override {
        if (std::filesystem::exists(test_file)) {
            std::filesystem::remove(test_file);
        }
    }

    void TearDown() override {
        if (std::filesystem::exists(test_file)) {
            std::filesystem::remove(test_file);
        }
    }
};

TEST_F(SSTableFullCycleTest, MassiveDataAndRandomRead) {
    // 1. 准备数据：使用 std::map 确保 Key 是天然有序的，模拟 Builder 的要求
    std::map<std::string, std::string> mock_data;
    for (int i = 0; i < 2000; ++i) {
        // 生成如 key_0001, key_0002 这种固定长度的 key，方便对比
        char buf[20];
        snprintf(buf, sizeof(buf), "key_%05d", i);
        mock_data[buf] = "value_of_" + std::string(buf);
    }

    // 2. 写入阶段 (狠狠考验 Builder 的分块和索引生成)
    {
        WritableFile file(test_file);
        SSTableBuilder builder(&file);
        for (const auto& [key, val] : mock_data) {
            builder.Add(key, val);
        }
        builder.Finish();
        // builder 析构，文件关闭
    }

    // 3. 读取阶段 (狠狠考验 Reader 的 mmap, Footer 和 IndexBlock)
    SSTableReader* reader = SSTableReader::Open(test_file);
    ASSERT_NE(reader, nullptr) << "Failed to open SSTable via Reader!";

    // 4. 验证阶段：精准打击
    // A. 验证所有存入的数据都能原样读出
    for (const auto& [key, expected_val] : mock_data) {
        std::string actual_val;
        bool found = reader->Get(key, &actual_val);
        EXPECT_TRUE(found) << "Key not found: " << key;
        EXPECT_EQ(actual_val, expected_val) << "Value mismatch for key: " << key;
    }

    // B. 验证边界值：第一个和最后一个
    std::string first_val;
    EXPECT_TRUE(reader->Get("key_00000", &first_val));
    EXPECT_EQ(first_val, "value_of_key_00000");

    // C. 验证不存在的 Key：
    std::string dummy;
    EXPECT_FALSE(reader->Get("key_99999", &dummy)) << "Should not find non-existent key";
    EXPECT_FALSE(reader->Get("abc", &dummy)) << "Should not find key smaller than min_key";
    EXPECT_FALSE(reader->Get("key_00000_extra", &dummy));

    // 5. 资源清理
    delete reader;
}