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

message(STATUS "C++ tests configured")

