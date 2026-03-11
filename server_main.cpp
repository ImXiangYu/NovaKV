//
// Created by 26708 on 2026/3/11.
//

#include "DBImpl.h"
#include "include/network/TcpServer.h"

int main() {
  const std::string kDbPath = "./data";

  try {
    constexpr uint16_t kPort = 6379;
    DBImpl db(kDbPath);
    TcpServer server(&db);

    if (!server.Start(kPort)) {
      std::cerr << "Failed to start server on port " << kPort << '\n';
      return 1;
    }

    std::cout << "NovaKV server listening on port " << kPort << '\n';
    server.Run();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Fatal server error: " << ex.what() << '\n';
    return 1;
  }
}