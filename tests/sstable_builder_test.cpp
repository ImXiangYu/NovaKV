//
// Created by 26708 on 2026/2/5.
//

#include <gtest/gtest.h>
#include "Storage.h"
#include "FileFormats.h"
#include "BlockBuilder.h"
#include "SSTableBuilder.h"
#include <filesystem>
#include <fstream>

class SSTableBuilderTest : public ::testing::Test {
    protected:
        void SetUp() override {
            filename_ = "test_data.sst";
            // 确保测试前文件不存在
            std::filesystem::remove(filename_);
            file_ = new WritableFile(filename_);
            builder_ = new SSTableBuilder(file_);
        }

        void TearDown() override {
            delete builder_;
            delete file_;
            // std::filesystem::remove(filename_); // 如果想肉眼看 hexdump，可以先注释掉这行
        }

        std::string filename_;
        WritableFile* file_{};
        SSTableBuilder* builder_{};
};

TEST_F(SSTableBuilderTest, LayoutAndIndexTest) {
    // 1. 前面的 Add 和 Finish 逻辑保持不变...
    for (int i = 0; i < 100; ++i) {
        builder_->Add("key_" + std::to_string(i), std::string(100, 'x'));
    }
    builder_->Finish();
    file_->Close();

    // 2. 验证文件物理大小
    uint64_t file_size = std::filesystem::file_size(filename_);

    // 3. 【新增】硬核验证：手动读取 Footer
    std::ifstream in(filename_, std::ios::binary);
    in.seekg(file_size - Footer::kEncodedLength); // 定位到最后 24 字节

    std::string footer_data(Footer::kEncodedLength, '\0');
    in.read(&footer_data[0], Footer::kEncodedLength);

    Footer decoded_footer;
    bool decode_ok = decoded_footer.DecodeFrom(footer_data);

    // 断言：魔数必须匹配，解析必须成功
    ASSERT_TRUE(decode_ok) << "Footer decode failed! Magic number might be wrong.";

    // 断言：Footer 里记录的 Index 位置应该在 Data Block 之后
    EXPECT_GT(decoded_footer.index_handle.offset, 0);
    EXPECT_GT(decoded_footer.index_handle.size, 0);

    std::cout << "Verified Footer: Index Offset=" << decoded_footer.index_handle.offset
              << ", Index Size=" << decoded_footer.index_handle.size << std::endl;
}