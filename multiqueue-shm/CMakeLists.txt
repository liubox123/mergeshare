# MultiQueue-SHM - 多进程共享内存流处理框架

cmake_minimum_required(VERSION 3.15)
project(MultiQueueSHM VERSION 2.0.0 LANGUAGES CXX)

# C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 查找依赖
find_package(Threads REQUIRED)
find_package(Boost REQUIRED)

# 启用测试
enable_testing()

# 子目录
add_subdirectory(core)
add_subdirectory(tests/cpp)

# 安装配置
include(GNUInstallDirs)

# 显示配置信息
message(STATUS "")
message(STATUS "MultiQueue-SHM Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Boost Version: ${Boost_VERSION}")
message(STATUS "")
