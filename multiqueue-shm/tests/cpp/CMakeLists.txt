# C++ 测试

# 启用测试
enable_testing()

# 查找 Google Test
find_package(GTest REQUIRED)

# Phase 1 & Phase 2 测试（新架构）
set(NEW_ARCH_TESTS
    test_types
    test_timestamp
    test_buffer_metadata
    test_buffer_pool
    test_buffer_allocator
    test_port_queue
    test_port_queue_multicast
    test_multicast_simple
    test_multicast_simple2
    test_debug_alloc
    test_block
    test_shm_manager
    test_runtime_simple
    test_integration
    test_integration_complete
    test_multiprocess
    test_multiprocess_advanced
    test_multiprocess_integration
    test_multiprocess_multicast
    test_continuous_stream
    test_timestamp_sync_streams
    test_integration_complete_streams
)

# 为每个测试创建可执行文件
foreach(TEST_NAME ${NEW_ARCH_TESTS})
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME}
        multiqueue_core
        GTest::gtest
        GTest::gtest_main
        Threads::Threads
    )
    if(TARGET Boost::boost)
        target_link_libraries(${TEST_NAME} Boost::boost)
    endif()
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

# 设置测试超时
set_tests_properties(test_shm_manager PROPERTIES TIMEOUT 30)
set_tests_properties(test_integration PROPERTIES TIMEOUT 60)
set_tests_properties(test_integration_complete PROPERTIES TIMEOUT 90)
set_tests_properties(test_multiprocess PROPERTIES TIMEOUT 30)
set_tests_properties(test_multiprocess_advanced PROPERTIES TIMEOUT 60)
set_tests_properties(test_multiprocess_integration PROPERTIES TIMEOUT 120)

# ===== 旧架构测试（兼容）=====

# Google Test 单元测试（如果可用）
include(${CMAKE_SOURCE_DIR}/cmake/FindGoogleTest.cmake)

if(GOOGLETEST_FOUND)
    message(STATUS "Google Test found, building unit tests")
    
    # 元数据测试
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_metadata.cpp)
        add_executable(test_metadata_old test_metadata.cpp)
        target_link_libraries(test_metadata_old
            multiqueue_core
            GTest::gtest
            GTest::gtest_main
            Threads::Threads
        )
        add_test(NAME MetadataTest COMMAND test_metadata_old)
    endif()
    
    # 配置测试
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_config.cpp)
        add_executable(test_config_old test_config.cpp)
        target_link_libraries(test_config_old
            multiqueue_core
            GTest::gtest
            GTest::gtest_main
            Threads::Threads
        )
        add_test(NAME ConfigTest COMMAND test_config_old)
    endif()
    
    # 日志测试
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_logger.cpp)
        add_executable(test_logger_old test_logger.cpp)
        target_link_libraries(test_logger_old
            multiqueue_core
            GTest::gtest
            GTest::gtest_main
            Threads::Threads
        )
        add_test(NAME LoggerTest COMMAND test_logger_old)
    endif()
    
    # 压力测试
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_stress.cpp)
        add_executable(test_stress_old test_stress.cpp)
        target_link_libraries(test_stress_old
            multiqueue_core
            GTest::gtest
            GTest::gtest_main
            Threads::Threads
        )
        add_test(NAME StressTest COMMAND test_stress_old)
    endif()
    
    # RingQueue 测试
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_ringqueue.cpp)
        add_executable(test_ringqueue_old test_ringqueue.cpp)
        target_link_libraries(test_ringqueue_old
            multiqueue_core
            GTest::gtest
            GTest::gtest_main
            Threads::Threads
        )
        if(TARGET Boost::boost)
            target_link_libraries(test_ringqueue_old Boost::boost)
        endif()
        add_test(NAME RingQueueTest COMMAND test_ringqueue_old)
    endif()
    
    # 时间戳同步测试
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_timestamp_sync.cpp)
        add_executable(test_timestamp_sync_old test_timestamp_sync.cpp)
        target_link_libraries(test_timestamp_sync_old
            multiqueue_core
            GTest::gtest
            GTest::gtest_main
            Threads::Threads
        )
        if(TARGET Boost::boost)
            target_link_libraries(test_timestamp_sync_old Boost::boost)
        endif()
        add_test(NAME TimestampSyncTest COMMAND test_timestamp_sync_old)
    endif()
    
    message(STATUS "Google Test unit tests configured")
else()
    message(STATUS "Google Test not found, skipping unit tests")
    message(STATUS "  Install: brew install googletest (macOS)")
    message(STATUS "          sudo apt install libgtest-dev (Ubuntu)")
endif()

message(STATUS "C++ tests configured")
