//
// Created by 26708 on 2026/2/10.
//

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>

#include "FileFormats.h"
#include "SSTableBuilder.h"
#include "SSTableReader.h"

class SSTableReaderForEachTest : public ::testing::Test {
protected:
    const std::string test_file = "foreach_test.sst";

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

TEST_F(SSTableReaderForEachTest, ForEachReturnsAllKeyValues) {
    std::map<std::string, std::string> expected;
    for (int i = 0; i < 200; ++i) {
        char buf[20];
        snprintf(buf, sizeof(buf), "key_%04d", i);
        expected[buf] = std::string("value_") + buf;
    }

    {
        WritableFile file(test_file);
        SSTableBuilder builder(&file);
        for (const auto& [key, value] : expected) {
            builder.Add(key, value, ValueType::kValue);
        }
        builder.Finish();
    }

    SSTableReader* reader = SSTableReader::Open(test_file);
    ASSERT_NE(reader, nullptr);

    std::map<std::string, std::string> actual;
    reader->ForEach([&actual](const std::string& key, const std::string& value) {
        actual[key] = value;
    });

    EXPECT_EQ(actual.size(), expected.size());
    EXPECT_EQ(actual["key_0000"], "value_key_0000");
    EXPECT_EQ(actual["key_0199"], "value_key_0199");

    delete reader;
}
