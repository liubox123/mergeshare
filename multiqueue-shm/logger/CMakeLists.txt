# Logger Component CMakeLists.txt

add_library(mp_logger INTERFACE)

target_include_directories(mp_logger INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# 依赖：需要文件系统支持
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    # GCC < 9 需要链接 stdc++fs
    target_link_libraries(mp_logger INTERFACE stdc++fs)
endif()

target_link_libraries(mp_logger INTERFACE
    Threads::Threads
)

# C++17 要求
target_compile_features(mp_logger INTERFACE cxx_std_17)

message(STATUS "Logger component configured (header-only)")


