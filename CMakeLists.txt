cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

project(VulkanPathTracer)

set(PROJECT_OUTPUT_DIR ${CMAKE_BINARY_DIR}/${PROJECT_NAME})

include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${PROJECT_SOURCE_DIR}/Libraries")

add_subdirectory(VulkanHelper)
add_subdirectory(PathTracer)