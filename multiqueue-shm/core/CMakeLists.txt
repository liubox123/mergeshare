# MultiQueue-SHM 核心库

# 核心库是header-only的，但我们创建一个接口库以便管理依赖
add_library(multiqueue_core INTERFACE)

# 设置包含目录
target_include_directories(multiqueue_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# 依赖 Boost
find_package(Boost REQUIRED)
target_link_libraries(multiqueue_core INTERFACE
    Boost::headers
    ${CMAKE_THREAD_LIBS_INIT}
)

# 安装头文件
install(DIRECTORY include/multiqueue
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)

# 安装目标
install(TARGETS multiqueue_core
    EXPORT MultiQueueSHMTargets
    INCLUDES DESTINATION include
)
