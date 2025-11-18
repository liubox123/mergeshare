# Core Library CMakeLists.txt

# 核心库是 header-only，不需要编译
# 但我们可以创建一个 interface library 来管理依赖

add_library(multiqueue_core INTERFACE)

target_include_directories(multiqueue_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(multiqueue_core INTERFACE
    Boost::system
    Threads::Threads
    mp_logger
)

if(ENABLE_TRACY)
    target_link_libraries(multiqueue_core INTERFACE tracy_wrapper)
    target_compile_definitions(multiqueue_core INTERFACE TRACY_ENABLE)
endif()

# C++17 要求
target_compile_features(multiqueue_core INTERFACE cxx_std_17)

# 安装
install(TARGETS multiqueue_core
    EXPORT multiqueue_core-targets
)

install(EXPORT multiqueue_core-targets
    FILE multiqueue_core-config.cmake
    NAMESPACE multiqueue::
    DESTINATION lib/cmake/multiqueue-shm
)

message(STATUS "Core library configured (header-only)")

