cmake_minimum_required(VERSION 3.10)
project(SharedMemoryStream LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 明确指定 Python 3.12 路径

set(PYBIND11_PYTHON_VERSION 3.12 CACHE STRING "")
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
if(Python3_FOUND)
    message(STATUS "Python_EXECUTABLE: ${Python3_EXECUTABLE}")
    message(STATUS "Python_INCLUDE_DIRS: ${Python3_INCLUDE_DIRS}")
    message(STATUS "Python_LIBRARIES: ${Python3_LIBRARIES}")
endif()

get_filename_component(PYTHON_ROOT_DIR "${Python3_EXECUTABLE}" DIRECTORY)
message(STATUS "PYTHON_ROOT_DIR: ${PYTHON_ROOT_DIR}")

add_definitions(-DBUILD_PYBIND)
add_definitions(${PYTHON_INCLUDES})
message(STATUS "PYTHON_LDFLAGS: ${PYTHON_LDFLAGS}")

	
find_package(Boost REQUIRED )

# 设置可能的 pybind11 路径
set(PYBIND11_PATHS
	"${PYTHON_ROOT_DIR}/Lib/site-packages/pybind11"
	"${PYTHON_ROOT_DIR}/Library/share/cmake/pybind11"
	"${PYTHON_ROOT_DIR}/share/cmake/pybind11"
)
# 关键修改1：直接指定 pybind11 安装路径
set(pybind11_DIR "${Python3_EXECUTABLE}/../Lib/site-packages/pybind11/share/cmake/pybind11")
message(STATUS "Looking for pybind11 in: ${pybind11_DIR}")

# 关键修改2：使用 CONFIG 模式查找
find_package(pybind11 REQUIRED CONFIG)
if(pybind11_FOUND)
    message(STATUS "Found pybind11 v${pybind11_VERSION}")
    message(STATUS "pybind11 include dir: ${pybind11_INCLUDE_DIR}")
endif()

if(pybind11_FOUND)
	message(STATUS "Using Python interpreter: ${Python_EXECUTABLE}")
	message(STATUS "pybind11 version: ${pybind11_VERSION}")


    pybind11_add_module(shared_ring_queue MODULE main.cpp shared_ring_queue.cpp)
    set_target_properties(shared_ring_queue PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/python/processor_decorator
)
    target_include_directories(shared_ring_queue PRIVATE ${Boost_INCLUDE_DIRS})
	
    message(STATUS "Python333_LIBRARIES: ${Python3_LIBRARIES}")
	
    target_link_directories(shared_ring_queue PRIVATE ${Python_LIBRARY_DIR})
	
    target_link_libraries(shared_ring_queue PRIVATE ${Boost_LIBRARIES} ${Python3_LIBRARIES})
    execute_process(
		COMMAND "${Python3_EXECUTABLE}" -c "import site; print(site.getsitepackages()[0])"
		OUTPUT_VARIABLE PYTHON3_SITE_PACKAGES
		OUTPUT_STRIP_TRAILING_WHITESPACE
		)
    install(TARGETS shared_ring_queue
        LIBRARY DESTINATION "${PYTHON3_SITE_PACKAGES}"
    )
    

endif() 