#include "network/CommandExecutor.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::string DrainBuffer(NetworkBuffer& buffer) {
  std::string output(buffer.Peek(), buffer.ReadableBytes());
  buffer.RetrieveAll();
  return output;
}
}  // namespace

class CommandExecutorTest : public ::testing::Test {
 protected:
  std::string test_db_path = "./test_command_executor_db";

  void SetUp() override {
    if (fs::exists(test_db_path)) {
      fs::remove_all(test_db_path);
    }
    fs::create_directories(test_db_path);
  }
};

TEST_F(CommandExecutorTest, SetGetDelAndScanRoundTrip) {
  DBImpl db(test_db_path);
  CommandExecutor executor(&db);
  NetworkBuffer response;

  executor.Execute({"SET", "alpha", "1"}, &response);
  EXPECT_EQ(DrainBuffer(response), "+OK\r\n");

  executor.Execute({"GET", "alpha"}, &response);
  EXPECT_EQ(DrainBuffer(response), "$1\r\n1\r\n");

  executor.Execute({"SET", "beta", "2"}, &response);
  EXPECT_EQ(DrainBuffer(response), "+OK\r\n");

  executor.Execute({"RSCAN", "alpha"}, &response);
  EXPECT_EQ(DrainBuffer(response),
            "*4\r\n$5\r\nalpha\r\n$1\r\n1\r\n$4\r\nbeta\r\n$1\r\n2\r\n");

  executor.Execute({"DEL", "alpha"}, &response);
  EXPECT_EQ(DrainBuffer(response), "+OK\r\n");

  executor.Execute({"GET", "alpha"}, &response);
  EXPECT_EQ(DrainBuffer(response), "$-1\r\n");

  executor.Execute({"RSCAN", "alpha"}, &response);
  EXPECT_EQ(DrainBuffer(response), "*2\r\n$4\r\nbeta\r\n$1\r\n2\r\n");
}

TEST_F(CommandExecutorTest, CommandNameIsCaseInsensitive) {
  DBImpl db(test_db_path);
  CommandExecutor executor(&db);
  NetworkBuffer response;

  executor.Execute({"set", "k", "v"}, &response);
  EXPECT_EQ(DrainBuffer(response), "+OK\r\n");

  executor.Execute({"gEt", "k"}, &response);
  EXPECT_EQ(DrainBuffer(response), "$1\r\nv\r\n");
}

TEST_F(CommandExecutorTest, ReturnsStandardRespErrorsForBadRequests) {
  DBImpl db(test_db_path);
  CommandExecutor executor(&db);
  NetworkBuffer response;

  executor.Execute({"PING"}, &response);
  EXPECT_EQ(DrainBuffer(response), "-ERR unknown command 'PING'\r\n");

  executor.Execute({"GET"}, &response);
  EXPECT_EQ(DrainBuffer(response),
            "-ERR wrong number of arguments for 'GET' command\r\n");

  executor.Execute({}, &response);
  EXPECT_EQ(DrainBuffer(response), "-ERR empty command\r\n");
}
