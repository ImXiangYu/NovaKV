//
// Simple QPS benchmark for NovaKV using google/benchmark.
//

#include <benchmark/benchmark.h>

#include <filesystem>
#include <string>
#include <vector>

#include "DBImpl.h"
#include "Logger.h"

namespace fs = std::filesystem;

namespace {
const char kBenchDir[] = "bench_db";

void PrepareDbDir() {
  std::error_code ec;
  fs::remove_all(kBenchDir, ec);
  fs::create_directories(kBenchDir, ec);
}

inline void PutValue(DBImpl& db, const std::string& key,
                     const std::string& value) {
  ValueRecord record{ValueType::kValue, value};
  db.Put(key, record);
}

inline bool GetValue(const DBImpl& db, const std::string& key,
                     std::string& value) {
  ValueRecord record{ValueType::kValue, ""};
  if (!db.Get(key, record)) {
    return false;
  }
  if (record.type == ValueType::kDeletion) {
    return false;
  }
  value = record.value;
  return true;
}
}  // namespace

static void BenchPut(benchmark::State& state) {
  Logger::SetLevel(LogLevel::Off);
  PrepareDbDir();
  DBImpl db(kBenchDir);

  // 启动后台监控线程（漂亮的状态输出）
  std::atomic<bool> quit{false};
  std::thread monitor([&db, &quit]() {
    while (!quit) {
      std::this_thread::sleep_for(std::chrono::seconds(2));
      auto s = db.GetStatus();
      printf(
          "\n[STAT] Mem:%zu | Imm:%zu | L0:%zu | L1:%zu | MinorCount:%lu | "
          "LastMinor:%lldms\n",
          s.mem_count, s.imm_count, s.l0_count, s.l1_count,
          s.minor_compact_count, s.last_minor_duration_ms);
      fflush(stdout);
    }
  });

  const std::string value(128, 'v');
  int64_t i = 0;
  for (auto _ : state) {
    std::string key = "key_" + std::to_string(i++);
    benchmark::DoNotOptimize(key);
    PutValue(db, key, value);
  }

  quit = true;
  if (monitor.joinable()) monitor.join();

  state.SetItemsProcessed(state.iterations());
}

static void BenchGet(benchmark::State& state) {
  Logger::SetLevel(LogLevel::Off);
  PrepareDbDir();
  DBImpl db(kBenchDir);

  const size_t preload = 10000;
  const std::string value(128, 'v');
  std::vector<std::string> keys;
  keys.reserve(preload);
  for (size_t i = 0; i < preload; ++i) {
    std::string key = "key_" + std::to_string(i);
    PutValue(db, key, value);
    keys.push_back(key);
  }

  std::string out;
  size_t idx = 0;
  for (auto _ : state) {
    const std::string& key = keys[idx++ % keys.size()];
    benchmark::DoNotOptimize(key);
    GetValue(db, key, out);
    benchmark::DoNotOptimize(out);
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BenchPut);
BENCHMARK(BenchGet);

int main(int argc, char** argv) {
  Logger::SetLevel(LogLevel::Off);
  ::benchmark::Initialize(&argc, argv);
  if (::benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
  ::benchmark::RunSpecifiedBenchmarks();
  return 0;
}
