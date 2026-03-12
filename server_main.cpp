//
// Created by 26708 on 2026/3/11.
//

#include <csignal>
#include <iostream>

#include "DBImpl.h"
#include "Logger.h"
#include "network/TcpServer.h"

namespace {

TcpServer* g_server = nullptr;

void HandleStopSignal(int) {
  LOG_INFO("received stop signal");
  if (g_server != nullptr) {
    g_server->Stop();
  }
}

}  // namespace

int main() {
  const std::string kDbPath = "./data";

  try {
    constexpr uint16_t kPort = 6379;
    DBImpl db(kDbPath);
    TcpServer server(&db);
    g_server = &server;

    std::signal(SIGINT, HandleStopSignal);
    std::signal(SIGTERM, HandleStopSignal);

    if (!server.Start(kPort)) {
      std::cerr << "Failed to start server on port " << kPort << '\n';
      g_server = nullptr;
      return 1;
    }

    std::cout << "NovaKV server listening on port " << kPort << '\n';
    server.Run();
    g_server = nullptr;
    return 0;
  } catch (const std::exception& ex) {
    g_server = nullptr;
    std::cerr << "Fatal server error: " << ex.what() << '\n';
    return 1;
  }
}
