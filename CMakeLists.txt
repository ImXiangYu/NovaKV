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

# 1. 搜集所有测试源码文件
file(GLOB TEST_FILES "tests/*.cpp")

# 2. 循环处理每一个文件，将其注册为独立的测试目标
foreach(test_file ${TEST_FILES})
    # 获取文件名（不带后缀），作为 Target 名字
    get_filename_component(test_name ${test_file} NAME_WE)

    # 为该文件创建独立的可执行文件
    # 注意：这里也链接了 ${SOURCES}，确保测试能用到 src 里的代码
    add_executable(${test_name} ${test_file} ${SOURCES})

    # 链接必要的库：gtest_main (自动入口), gtest (核心), pthread (线程支持)
    target_link_libraries(${test_name} gtest_main gtest pthread)

    # 将其注册到 CTest 框架中，方便一键跑所有测试
    add_test(NAME ${test_name} COMMAND ${test_name})

    message(STATUS "Created test target: ${test_name}")
endforeach()

# 启用 ctest 命令行工具
enable_testing()
add_test(NAME AllTests COMMAND unit_tests)