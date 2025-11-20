# Tracy Integration CMakeLists.txt

# Tracy 包装器是 header-only
add_library(tracy_wrapper INTERFACE)

target_include_directories(tracy_wrapper INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# 如果启用 Tracy，添加 Tracy 依赖
if(ENABLE_TRACY)
    # 尝试查找 Tracy
    find_package(Tracy QUIET)
    
    if(Tracy_FOUND)
        message(STATUS "Tracy found, linking against Tracy library")
        target_link_libraries(tracy_wrapper INTERFACE Tracy::TracyClient)
    else()
        # 如果找不到 Tracy 包，尝试手动配置
        message(STATUS "Tracy not found as package, checking for tracy/Tracy.hpp")
        
        # 检查是否可以找到 Tracy.hpp
        find_path(TRACY_INCLUDE_DIR 
            NAMES tracy/Tracy.hpp
            PATHS
                /usr/include
                /usr/local/include
                /opt/homebrew/include
                ${CMAKE_SOURCE_DIR}/third_party/tracy
        )
        
        if(TRACY_INCLUDE_DIR)
            message(STATUS "Tracy include directory found: ${TRACY_INCLUDE_DIR}")
            target_include_directories(tracy_wrapper INTERFACE ${TRACY_INCLUDE_DIR})
        else()
            message(WARNING "Tracy Profiler not found. Tracy features will be disabled.")
            message(STATUS "To use Tracy:")
            message(STATUS "  1. Download Tracy from https://github.com/wolfpld/tracy")
            message(STATUS "  2. Install Tracy headers or set TRACY_INCLUDE_DIR")
            message(STATUS "  3. Or place Tracy in third_party/tracy/")
        endif()
    endif()
    
    # 添加 Tracy 编译定义
    target_compile_definitions(tracy_wrapper INTERFACE TRACY_ENABLE)
    
    # Tracy 需要线程支持
    target_link_libraries(tracy_wrapper INTERFACE Threads::Threads)
    
    # Tracy 在某些平台需要额外的库
    if(UNIX AND NOT APPLE)
        target_link_libraries(tracy_wrapper INTERFACE dl)
    endif()
    
    message(STATUS "Tracy Profiler integration enabled")
else()
    message(STATUS "Tracy Profiler integration disabled (use -DENABLE_TRACY=ON to enable)")
endif()

message(STATUS "Tracy wrapper configured")


