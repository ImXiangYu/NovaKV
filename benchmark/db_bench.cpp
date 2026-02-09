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
} // namespace

static void BenchPut(benchmark::State& state) {
    Logger::SetLevel(LogLevel::Off);
    PrepareDbDir();
    DBImpl db(kBenchDir);

    const std::string value(128, 'v');
    int64_t i = 0;
    for (auto _ : state) {
        std::string key = "key_" + std::to_string(i++);
        benchmark::DoNotOptimize(key);
        db.Put(key, value);
    }

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
        db.Put(key, value);
        keys.push_back(key);
    }

    std::string out;
    size_t idx = 0;
    for (auto _ : state) {
        const std::string& key = keys[idx++ % keys.size()];
        benchmark::DoNotOptimize(key);
        db.Get(key, out);
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
