#include <gtest/gtest.h>

#include "network/NetworkBuffer.h"
#include "network/RESPParser.h"

class RESPParserTest : public ::testing::Test {
 protected:
  RESPParser parser;
  NetworkBuffer buffer;
};

// 1. 测试完整指令一次性解析
TEST_F(RESPParserTest, BasicSuccess) {
  const char* raw = "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n";
  buffer.Append(raw, strlen(raw));

  std::vector<std::string> cmd;
  ParseStatus status = parser.Parse(&buffer, cmd);

  EXPECT_EQ(status, ParseStatus::SUCCESS);
  ASSERT_EQ(cmd.size(), 3);
  EXPECT_EQ(cmd[0], "SET");
  EXPECT_EQ(cmd[1], "key");
  EXPECT_EQ(cmd[2], "value");
  EXPECT_EQ(buffer.ReadableBytes(), 0);
}

// 2. 测试半包（分段）解析 —— 网络库最核心的测试点
TEST_F(RESPParserTest, PartialPacket) {
  std::vector<std::string> cmd;

  // 第一段：只给个 *3\r (长度 3，正确)
  buffer.Append("*3\r", 3);
  EXPECT_EQ(parser.Parse(&buffer, cmd), ParseStatus::INCOMPLETE);

  // 第二段：补全 \n 和 $3\r\nSET (实际长度是 8)
  buffer.Append("\n$3\r\nSET", 8);
  EXPECT_EQ(parser.Parse(&buffer, cmd), ParseStatus::INCOMPLETE);

  // 第三段：补全 \r\n 并给一半 key (实际长度是 8)
  buffer.Append("\r\n$3\r\nke", 8);
  EXPECT_EQ(parser.Parse(&buffer, cmd), ParseStatus::INCOMPLETE);

  // 第四段：最后补全所有 (实际长度是 14)
  buffer.Append("y\r\n$5\r\nvalue\r\n", 14);
  EXPECT_EQ(parser.Parse(&buffer, cmd), ParseStatus::SUCCESS);

  ASSERT_EQ(cmd.size(), 3);
  EXPECT_EQ(cmd[0], "SET");
  EXPECT_EQ(cmd[1], "key");
  EXPECT_EQ(cmd[2], "value");
}

// 3. 测试指令粘包（一次收到两个指令）
TEST_F(RESPParserTest, StickyPackets) {
  const char* raw = "*1\r\n$4\r\nPING\r\n*1\r\n$4\r\nPING\r\n";
  buffer.Append(raw, strlen(raw));

  std::vector<std::string> cmd1;
  EXPECT_EQ(parser.Parse(&buffer, cmd1), ParseStatus::SUCCESS);
  EXPECT_EQ(cmd1[0], "PING");

  std::vector<std::string> cmd2;
  // 此时状态机应该已经通过 Reset() 准备好解析下一条了
  EXPECT_EQ(parser.Parse(&buffer, cmd2), ParseStatus::SUCCESS);
  EXPECT_EQ(cmd2[0], "PING");
}

// 4. 测试错误协议处理
TEST_F(RESPParserTest, ProtocolError) {
  const char* raw = "+NOTARRAY\r\n";  // 应该以 * 开头
  buffer.Append(raw, strlen(raw));

  std::vector<std::string> cmd;
  EXPECT_EQ(parser.Parse(&buffer, cmd), ParseStatus::ERROR);
}

// 5. 测试空数组（特殊边界）
TEST_F(RESPParserTest, EmptyArray) {
  const char* raw = "*0\r\n";
  buffer.Append(raw, strlen(raw));

  std::vector<std::string> cmd;
  EXPECT_EQ(parser.Parse(&buffer, cmd), ParseStatus::SUCCESS);
  EXPECT_TRUE(cmd.empty());
}

TEST_F(RESPParserTest, RejectsNegativeArrayLength) {
  const char* raw = "*-1\r\n";
  buffer.Append(raw, strlen(raw));

  std::vector<std::string> cmd;
  EXPECT_EQ(parser.Parse(&buffer, cmd), ParseStatus::ERROR);
}

TEST_F(RESPParserTest, RejectsTrailingGarbageInArrayLength) {
  const char* raw = "*2x\r\n";
  buffer.Append(raw, strlen(raw));

  std::vector<std::string> cmd;
  EXPECT_EQ(parser.Parse(&buffer, cmd), ParseStatus::ERROR);
}

TEST_F(RESPParserTest, AcceptsNullBulkString) {
  const char* raw = "*1\r\n$-1\r\n";
  buffer.Append(raw, strlen(raw));

  std::vector<std::string> cmd;
  EXPECT_EQ(parser.Parse(&buffer, cmd), ParseStatus::SUCCESS);
  ASSERT_EQ(cmd.size(), 1);
  EXPECT_EQ(cmd[0], "");
}

TEST_F(RESPParserTest, RejectsInvalidNegativeBulkLength) {
  const char* raw = "*1\r\n$-2\r\n";
  buffer.Append(raw, strlen(raw));

  std::vector<std::string> cmd;
  EXPECT_EQ(parser.Parse(&buffer, cmd), ParseStatus::ERROR);
}

TEST_F(RESPParserTest, RejectsTrailingGarbageInBulkLength) {
  const char* raw = "*1\r\n$3x\r\nabc\r\n";
  buffer.Append(raw, strlen(raw));

  std::vector<std::string> cmd;
  EXPECT_EQ(parser.Parse(&buffer, cmd), ParseStatus::ERROR);
}

TEST_F(RESPParserTest, RejectsBulkDataWithoutCRLFTerminator) {
  const char* raw = "*1\r\n$3\r\nabcxx";
  buffer.Append(raw, strlen(raw));

  std::vector<std::string> cmd;
  EXPECT_EQ(parser.Parse(&buffer, cmd), ParseStatus::ERROR);
}
