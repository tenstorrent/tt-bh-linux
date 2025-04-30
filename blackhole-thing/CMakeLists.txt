cmake_minimum_required(VERSION 3.10)
project(blackhole-thing)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(Threads REQUIRED)

add_subdirectory(third_party/fmt)
add_subdirectory(src)
include_directories(src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(console console.cpp)
target_link_libraries(console blackhole_thing)
