//
// Created by 26708 on 2026/3/9.
//

#include "network/CommandExecutor.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "network/RESPEncoder.h"

void CommandExecutor::Execute(const std::vector<std::string>& command,
                              NetworkBuffer* response_buffer) const {
  if (db_ == nullptr) {
    RESPEncoder::EncodeError(response_buffer, "db is not initialized");
    return;
  }

  if (command.empty()) {
    RESPEncoder::EncodeError(response_buffer, "empty command");
    return;
  }

  const std::string cmd = NormalizeCommandName(command[0]);

  if (cmd == "SET") {
    HandleSet(command, response_buffer);
    return;
  }
  if (cmd == "GET") {
    HandleGet(command, response_buffer);
    return;
  }
  if (cmd == "DEL") {
    HandleDel(command, response_buffer);
    return;
  }
  if (cmd == "RSCAN") {
    HandleRScan(command, response_buffer);
    return;
  }

  RESPEncoder::EncodeError(response_buffer, "unknown command '" + cmd + "'");
}

void CommandExecutor::HandleSet(const std::vector<std::string>& command,
                                NetworkBuffer* response_buffer) const {
  if (!ExpectArgCount(command, 3, response_buffer)) {
    return ;
  }

  const std::string& key = command[1];
  const std::string& value = command[2];

  ValueRecord record;
  record.type = ValueType::kValue;
  record.value = value;

  db_->Put(key, record);

  RESPEncoder::EncodeSimpleString(response_buffer, "OK");
}

void CommandExecutor::HandleGet(const std::vector<std::string>& command,
                                NetworkBuffer* response_buffer) const {
  if (!ExpectArgCount(command, 2, response_buffer)) {
    return ;
  }

  const std::string& key = command[1];

  ValueRecord record;
  record.type = ValueType::kValue;
  record.value = "";

  const bool found = db_->Get(key, record);

  if (!found) {
    RESPEncoder::EncodeNull(response_buffer);
    return;
  }

  RESPEncoder::EncodeBulkString(response_buffer, record.value);
}

void CommandExecutor::HandleDel(const std::vector<std::string>& command,
                                NetworkBuffer* response_buffer) const {
  if (!ExpectArgCount(command, 2, response_buffer)) {
    return ;
  }

  const std::string& key = command[1];

  ValueRecord record;
  record.type = ValueType::kDeletion;
  record.value = "";

  db_->Put(key, record);

  RESPEncoder::EncodeSimpleString(response_buffer, "OK");
}

void CommandExecutor::HandleRScan(const std::vector<std::string>& command,
                                  NetworkBuffer* response_buffer) const {
  if (!ExpectArgCount(command, 2, response_buffer)) {
    return ;
  }

  const std::string& start_key = command[1];

  const auto iter = db_->NewIterator();

  iter->Seek(start_key);

  std::vector<std::string> elements;

  while (iter->Valid()) {
    elements.emplace_back(iter->key());
    elements.emplace_back(iter->value());
    iter->Next();
  }

  RESPEncoder::EncodeArray(response_buffer, elements);
}

std::string CommandExecutor::NormalizeCommandName(
    const std::string& command_name) {
  std::string result = command_name;  // 创建副本

  // 使用 std::transform 进行原地转换
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::toupper(c); });

  return result;
}
bool CommandExecutor::ExpectArgCount(const std::vector<std::string>& command,
                                     size_t expected_argc,
                                     NetworkBuffer* response_buffer) {
  if (command.size() == expected_argc) {
    return true;
  }

  const std::string cmd = NormalizeCommandName(command[0]);
  RESPEncoder::EncodeError(response_buffer,
                           "wrong number of arguments for '" + cmd + "' command");
  return false;
}
