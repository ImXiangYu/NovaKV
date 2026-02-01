cmake_minimum_required(VERSION 3.10)
project(NovaKV)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 包含头文件路径 (让编译器能找到 include 文件夹里的 .h)
include_directories(include)

# 自动搜寻 src 目录下的所有源文件
file(GLOB_RECURSE SOURCES "src/*.cpp")

# 生成可执行文件 (暂时叫 nova_test，方便你写主程序测试跳表)
add_executable(nova_test main.cpp ${SOURCES})