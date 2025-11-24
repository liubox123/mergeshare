# C++ 测试

# 启用测试
enable_testing()

# 查找 Google Test
find_package(GTest REQUIRED)

# 测试可执行文件列表
set(TEST_SOURCES
    test_types.cpp
    test_timestamp.cpp
    test_buffer_metadata.cpp
    test_buffer_pool.cpp
    test_buffer_allocator.cpp
    test_port_queue.cpp
    test_block.cpp
    test_multiprocess.cpp
    test_multiprocess_advanced.cpp
)

# Google Test 单元测试（如果可用）
include(${CMAKE_SOURCE_DIR}/cmake/FindGoogleTest.cmake)

if(GOOGLETEST_FOUND)
    message(STATUS "Google Test found, building unit tests")
    
    # 元数据测试
    add_executable(test_metadata test_metadata.cpp)
    target_link_libraries(test_metadata
        multiqueue_core
        GTest::gtest
        GTest::gtest_main
        Threads::Threads
    )
    add_test(NAME MetadataTest COMMAND test_metadata)
    
    # 配置测试
    add_executable(test_config test_config.cpp)
    target_link_libraries(test_config
        multiqueue_core
        GTest::gtest
        GTest::gtest_main
        Threads::Threads
    )
    add_test(NAME ConfigTest COMMAND test_config)
    
    # 日志测试
    add_executable(test_logger test_logger.cpp)
    target_link_libraries(test_logger
        multiqueue_core
        GTest::gtest
        GTest::gtest_main
        Threads::Threads
    )
    add_test(NAME LoggerTest COMMAND test_logger)
    
    # 压力测试
    add_executable(test_stress test_stress.cpp)
    target_link_libraries(test_stress
        multiqueue_core
        GTest::gtest
        GTest::gtest_main
        Threads::Threads
    )
    add_test(NAME StressTest COMMAND test_stress)
    
    # RingQueue 测试
    add_executable(test_ringqueue test_ringqueue.cpp)
    target_link_libraries(test_ringqueue
        multiqueue_core
        GTest::gtest
        GTest::gtest_main
        Threads::Threads
    )
    # 添加 Boost 链接
    if(TARGET Boost::boost)
        target_link_libraries(test_ringqueue Boost::boost)
    endif()
    add_test(NAME RingQueueTest COMMAND test_ringqueue)
    
    # 时间戳同步测试
    add_executable(test_timestamp_sync test_timestamp_sync.cpp)
    target_link_libraries(test_timestamp_sync
        multiqueue_core
        GTest::gtest
        GTest::gtest_main
        Threads::Threads
    )
    if(TARGET Boost::boost)
        target_link_libraries(test_timestamp_sync Boost::boost)
    endif()
    add_test(NAME TimestampSyncTest COMMAND test_timestamp_sync)
    
    message(STATUS "Google Test unit tests configured")
else()
    message(STATUS "Google Test not found, skipping unit tests")
    message(STATUS "  Install: brew install googletest (macOS)")
    message(STATUS "          sudo apt install libgtest-dev (Ubuntu)")
endif()

message(STATUS "C++ tests configured")

# 设置测试超时
set_tests_properties(test_multiprocess PROPERTIES TIMEOUT 30)
set_tests_properties(test_multiprocess_advanced PROPERTIES TIMEOUT 60)
