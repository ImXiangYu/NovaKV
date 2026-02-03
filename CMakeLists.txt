cmake_minimum_required(VERSION 3.10)
project(NovaKV)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 包含路径
include_directories(include)

# 自动搜寻 src 下的逻辑代码（不包含 main.cpp）
file(GLOB_RECURSE SOURCES "src/*.cpp")

# --- 目标 1: 你的手动测试程序 (保留 main.cpp) ---
add_executable(nova_test main.cpp ${SOURCES})

# --- 目标 2: Google Test 单元测试 ---
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/heads/main.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# 搜索 test 目录下所有的测试源文件
file(GLOB_RECURSE TEST_SOURCES "tests/*.cpp")

# 创建测试程序
add_executable(unit_tests ${TEST_SOURCES} ${SOURCES})

# 链接 GTest 库 (gtest_main 会自动提供测试入口，不需要你写 main 函数)
target_link_libraries(unit_tests gtest_main)

# 启用 ctest 命令行工具
enable_testing()
add_test(NAME AllTests COMMAND unit_tests)