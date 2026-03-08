//
// Created by 26708 on 2026/3/8.
//

#include <gtest/gtest.h>

#include "network/NetworkBuffer.h"
#include "network/RESPEncoder.h"

class RESPEncoderTest : public ::testing::Test {
 protected:
  NetworkBuffer buffer;

  void SetUp() override { buffer.RetrieveAll(); }

  std::string GetBufferContent() {
    return std::string(buffer.Peek(), buffer.ReadableBytes());
  }
};

TEST_F(RESPEncoderTest, SimpleString) {
  RESPEncoder::EncodeSimpleString(&buffer, "OK");
  EXPECT_EQ(GetBufferContent(), "+OK\r\n");
}

TEST_F(RESPEncoderTest, Error) {
  RESPEncoder::EncodeError(&buffer, "Unknown Command");
  EXPECT_EQ(GetBufferContent(), "-ERR Unknown Command\r\n");
}

TEST_F(RESPEncoderTest, Integer) {
  RESPEncoder::EncodeInteger(&buffer, 100);
  EXPECT_EQ(GetBufferContent(), ":100\r\n");

  buffer.RetrieveAll();
  RESPEncoder::EncodeInteger(&buffer, -1);
  EXPECT_EQ(GetBufferContent(), ":-1\r\n");
}

TEST_F(RESPEncoderTest, BulkString) {
  RESPEncoder::EncodeBulkString(&buffer, "hello");
  EXPECT_EQ(GetBufferContent(), "$5\r\nhello\r\n");

  // 测试包含 \0 的二进制数据
  buffer.RetrieveAll();
  std::string binary_data("a\0b", 3);
  RESPEncoder::EncodeBulkString(&buffer, binary_data);
  EXPECT_EQ(buffer.ReadableBytes(),
            1 + 1 + 2 + 3 + 2);  // $ + 3 + \r\n + data + \r\n
  EXPECT_EQ(GetBufferContent(), std::string("$3\r\na\0b\r\n", 9));
}

TEST_F(RESPEncoderTest, Null) {
  RESPEncoder::EncodeNull(&buffer);
  EXPECT_EQ(GetBufferContent(), "$-1\r\n");
}

TEST_F(RESPEncoderTest, Array) {
  std::vector<std::string> elements = {"hello", "world"};
  RESPEncoder::EncodeArray(&buffer, elements);
  // 注意：按目前的实现，Array 内部调用的是 EncodeBulkString
  EXPECT_EQ(GetBufferContent(), "*2\r\n$5\r\nhello\r\n$5\r\nworld\r\n");
}
