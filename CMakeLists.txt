cmake_minimum_required(VERSION 3.11)

project(pmr LANGUAGES CXX C)
include(ExternalProject)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()
message("${CMAKE_BUILD_TYPE} Mode")

option(USE_DMABUF "use dmabuf" OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -fPIC -pthread -fmax-errors=5 -O2")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")

set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
find_library(URING NAMES /usr/lib uring)
message("uring: ${URING}")
link_libraries(${URING})

if(USE_DMABUF)
    message("<<<< Using SSD's CMB/PMR as Direct Accessed Memory >>>>")
    add_compile_options(-I/usr/src/linux-headers-5.14.0+/)
    add_compile_definitions(DMABUF)
endif()

ExternalProject_Add(
    YCSB-Gen
    GIT_REPOSITORY "https://gitee.com/ypluo18/YCSB-Gen.git"
    GIT_TAG "master"
    UPDATE_COMMAND ""
    INSTALL_COMMAND ""
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_BUILD ON
)

include_directories(include)
include_directories(fontend)
include_directories(rdmautil)
include_directories(database/leveldb/include)

add_subdirectory(rdmautil)
add_subdirectory(database)
add_subdirectory(frontend)

add_executable(server server.cc)
target_link_libraries(server frontend leveldb)

add_executable(benchmark benchmark.cc)
target_link_libraries(benchmark frontend leveldb)

add_executable(test test.cc)
target_link_libraries(test frontend leveldb)