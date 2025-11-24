# Python Binding CMakeLists.txt

message(STATUS "Configuring Python binding (pybind11)")

# 查找 Python
find_package(Python3 COMPONENTS Interpreter Development QUIET)

if(NOT Python3_FOUND)
    message(WARNING "Python3 not found, skipping Python binding")
    message(STATUS "  Install Python3 development package:")
    message(STATUS "    macOS: brew install python3")
    message(STATUS "    Ubuntu: sudo apt install python3-dev")
    return()
endif()

message(STATUS "Python3 found: ${Python3_VERSION}")
message(STATUS "  Interpreter: ${Python3_EXECUTABLE}")
message(STATUS "  Include: ${Python3_INCLUDE_DIRS}")

# 查找 pybind11
find_package(pybind11 QUIET)

if(NOT pybind11_FOUND)
    message(WARNING "pybind11 not found, skipping Python binding")
    message(STATUS "  Install pybind11:")
    message(STATUS "    pip3 install pybind11")
    message(STATUS "    Or: brew install pybind11 (macOS)")
    message(STATUS "    Or: sudo apt install pybind11-dev (Ubuntu)")
    return()
endif()

message(STATUS "pybind11 found: ${pybind11_VERSION}")

# 创建 Python 扩展模块
pybind11_add_module(multiqueue_shm src/multiqueue_python.cpp)

# 链接核心库
target_link_libraries(multiqueue_shm PRIVATE
    multiqueue_core
    Threads::Threads
)

# 添加 Boost 链接
if(TARGET Boost::boost)
    target_link_libraries(multiqueue_shm PRIVATE Boost::boost)
endif()

# 设置输出目录
set_target_properties(multiqueue_shm PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/python
)

# 安装 Python 模块
install(TARGETS multiqueue_shm
    LIBRARY DESTINATION ${Python3_SITEARCH}
)

message(STATUS "Python binding configured successfully")
message(STATUS "  Module: multiqueue_shm")
message(STATUS "  Output: ${CMAKE_BINARY_DIR}/python")

