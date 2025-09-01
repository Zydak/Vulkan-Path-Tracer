cmake_minimum_required(VERSION 3.12)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

project(VulkanPathTracer)

include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${PROJECT_SOURCE_DIR}/Libraries")

add_subdirectory(VulkanHelper)
add_subdirectory(PathTracer)