# FindGoogleTest.cmake
# 查找 Google Test 库

# 尝试使用 find_package 查找 GTest
find_package(GTest QUIET)

if(GTest_FOUND OR GTEST_FOUND)
    message(STATUS "Google Test found")
    
    # 确保目标存在
    if(NOT TARGET GTest::gtest)
        if(TARGET GTest::GTest)
            add_library(GTest::gtest ALIAS GTest::GTest)
        endif()
    endif()
    
    if(NOT TARGET GTest::gtest_main)
        if(TARGET GTest::Main)
            add_library(GTest::gtest_main ALIAS GTest::Main)
        endif()
    endif()
    
    set(GOOGLETEST_FOUND TRUE)
    return()
endif()

# 如果 find_package 失败，尝试手动查找
message(STATUS "Google Test not found via find_package, searching manually...")

# 查找头文件
find_path(GTEST_INCLUDE_DIR
    NAMES gtest/gtest.h
    PATHS
        /usr/include
        /usr/local/include
        /opt/homebrew/include
        ${CMAKE_SOURCE_DIR}/third_party/googletest/googletest/include
)

# 查找库文件
find_library(GTEST_LIBRARY
    NAMES gtest
    PATHS
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
        ${CMAKE_SOURCE_DIR}/third_party/googletest/build/lib
)

find_library(GTEST_MAIN_LIBRARY
    NAMES gtest_main
    PATHS
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
        ${CMAKE_SOURCE_DIR}/third_party/googletest/build/lib
)

# 检查是否找到
if(GTEST_INCLUDE_DIR AND GTEST_LIBRARY)
    message(STATUS "Google Test found manually")
    message(STATUS "  Include: ${GTEST_INCLUDE_DIR}")
    message(STATUS "  Library: ${GTEST_LIBRARY}")
    
    # 创建导入目标
    if(NOT TARGET GTest::gtest)
        add_library(GTest::gtest UNKNOWN IMPORTED)
        set_target_properties(GTest::gtest PROPERTIES
            IMPORTED_LOCATION "${GTEST_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIR}"
        )
    endif()
    
    if(GTEST_MAIN_LIBRARY AND NOT TARGET GTest::gtest_main)
        add_library(GTest::gtest_main UNKNOWN IMPORTED)
        set_target_properties(GTest::gtest_main PROPERTIES
            IMPORTED_LOCATION "${GTEST_MAIN_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "GTest::gtest"
        )
    endif()
    
    set(GOOGLETEST_FOUND TRUE)
else()
    message(STATUS "Google Test not found")
    message(STATUS "To use Google Test:")
    message(STATUS "  1. Install: brew install googletest (macOS)")
    message(STATUS "             sudo apt install libgtest-dev (Ubuntu)")
    message(STATUS "  2. Or build from source in third_party/googletest/")
    
    set(GOOGLETEST_FOUND FALSE)
endif()


