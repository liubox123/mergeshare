# C++ Tests CMakeLists.txt

# 编译测试（验证接口定义）
add_executable(test_compile test_compile.cpp)
target_link_libraries(test_compile
    multiqueue_core
    Threads::Threads
)

# Boost 链接（如果需要）
if(TARGET Boost::boost)
    target_link_libraries(test_compile Boost::boost)
elseif(TARGET Boost::system)
    target_link_libraries(test_compile Boost::system)
endif()

# 添加编译测试到 CTest
add_test(NAME CompileTest COMMAND test_compile)

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

