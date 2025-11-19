# Examples CMakeLists.txt

# C++ Examples
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cpp/CMakeLists.txt")
    add_subdirectory(cpp)
else()
    message(STATUS "C++ examples CMakeLists.txt not found, skipping")
endif()

# Python Examples  
# Python examples don't need CMake configuration
if(BUILD_PYTHON_BINDING)
    message(STATUS "Python examples available in examples/python/")
endif()

message(STATUS "Examples configured (empty for now)")

