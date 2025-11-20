# Tests CMakeLists.txt

# C++ 测试
add_subdirectory(cpp)

# Python 测试（如果启用）
if(BUILD_PYTHON_BINDING)
    # Python 测试通过 pytest 运行，不需要在这里配置
    message(STATUS "Python tests will be run via pytest")
endif()

message(STATUS "Tests configured")


