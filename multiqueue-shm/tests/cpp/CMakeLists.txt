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
)

# 为每个测试源文件创建可执行文件
foreach(TEST_SOURCE ${TEST_SOURCES})
    # 获取测试名称（去掉 .cpp 扩展名）
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    
    # 创建可执行文件
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    
    # 链接库
    target_link_libraries(${TEST_NAME}
        multiqueue_core
        GTest::GTest
        GTest::Main
        ${CMAKE_THREAD_LIBS_INIT}
    )
    
    # 设置 C++ 标准
    set_target_properties(${TEST_NAME} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
    
    # 添加到测试列表
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

# 设置测试超时
set_tests_properties(test_multiprocess PROPERTIES TIMEOUT 30)
