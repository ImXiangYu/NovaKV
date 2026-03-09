#ifndef NOVAKV_COMMANDEXECUTOR_H
#define NOVAKV_COMMANDEXECUTOR_H

#include <string>
#include <vector>

#include "DBImpl.h"
#include "network/NetworkBuffer.h"

class CommandExecutor {
 public:
  explicit CommandExecutor(DBImpl* db) : db_(db) {}

  void Execute(const std::vector<std::string>& command,
               NetworkBuffer* response_buffer) const;

 private:
  void HandleSet(const std::vector<std::string>& command,
                 NetworkBuffer* response_buffer) const;
  void HandleGet(const std::vector<std::string>& command,
                 NetworkBuffer* response_buffer) const;
  void HandleDel(const std::vector<std::string>& command,
                 NetworkBuffer* response_buffer) const;
  void HandleRScan(const std::vector<std::string>& command,
                   NetworkBuffer* response_buffer) const;

  static std::string NormalizeCommandName(const std::string& command_name);
  static bool ExpectArgCount(const std::vector<std::string>& command,
                             size_t expected_argc,
                             NetworkBuffer* response_buffer);

  DBImpl* db_;
};

#endif  // NOVAKV_COMMANDEXECUTOR_H
