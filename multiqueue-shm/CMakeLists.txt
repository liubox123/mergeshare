cmake_minimum_required(VERSION 3.15)

project(multiqueue-shm 
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "High-performance Multi-Producer Multi-Consumer Shared Memory Queue Library"
)

# C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 项目选项
option(BUILD_PYTHON_BINDING "Build Python binding with pybind11" ON)
option(ENABLE_TRACY "Enable Tracy profiler integration" ON)
option(BUILD_TESTS "Build test programs" ON)
option(BUILD_EXAMPLES "Build example programs" ON)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

# 输出配置信息
message(STATUS "=================================================")
message(STATUS "MultiQueue-SHM Configuration")
message(STATUS "=================================================")
message(STATUS "Build Type:           ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:         C++${CMAKE_CXX_STANDARD}")
message(STATUS "Python Binding:       ${BUILD_PYTHON_BINDING}")
message(STATUS "Tracy Profiler:       ${ENABLE_TRACY}")
message(STATUS "Build Tests:          ${BUILD_TESTS}")
message(STATUS "Build Examples:       ${BUILD_EXAMPLES}")
message(STATUS "AddressSanitizer:     ${ENABLE_ASAN}")
message(STATUS "ThreadSanitizer:      ${ENABLE_TSAN}")
message(STATUS "=================================================")

# CMake 模块路径
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# 查找依赖库
find_package(Boost 1.70 REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

# 可选依赖
if(ENABLE_TRACY)
    find_package(Tracy QUIET)
    if(NOT Tracy_FOUND)
        message(WARNING "Tracy not found, will use header-only mode")
    endif()
endif()

# 编译选项设置
if(MSVC)
    # Windows (MSVC)
    add_compile_options(/W4 /WX)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /GL)
    endif()
else()
    # Linux / macOS (GCC / Clang)
    add_compile_options(-Wall -Wextra -Werror)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -march=native -flto)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
    endif()
endif()

# Sanitizers
if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
    message(STATUS "AddressSanitizer enabled")
endif()

if(ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
    message(STATUS "ThreadSanitizer enabled")
endif()

# 包含目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${Boost_INCLUDE_DIRS})

# 子目录
add_subdirectory(logger)
add_subdirectory(core)

if(ENABLE_TRACY)
    add_subdirectory(tracy-integration)
endif()

if(BUILD_PYTHON_BINDING)
    add_subdirectory(python-binding)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# 安装规则
install(DIRECTORY core/include/ 
    DESTINATION include/multiqueue-shm
    FILES_MATCHING PATTERN "*.hpp"
)

install(DIRECTORY logger/include/ 
    DESTINATION include/multiqueue-shm
    FILES_MATCHING PATTERN "*.hpp"
)

if(ENABLE_TRACY)
    install(DIRECTORY tracy-integration/
        DESTINATION include/multiqueue-shm/tracy
        FILES_MATCHING PATTERN "*.hpp"
    )
endif()

# 打包配置
set(CPACK_PACKAGE_NAME "multiqueue-shm")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VENDOR "MultiQueue-SHM Team")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

include(CPack)

# 输出总结
message(STATUS "=================================================")
message(STATUS "Configuration completed successfully!")
message(STATUS "Run 'cmake --build .' to build the project")
message(STATUS "=================================================")

