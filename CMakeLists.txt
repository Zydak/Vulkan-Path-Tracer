cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

project(VulkanPathTracer)

set(PROJECT_OUTPUT_DIR ${CMAKE_BINARY_DIR}/${PROJECT_NAME})

include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${PROJECT_SOURCE_DIR}/Libraries")

FetchContent_Declare(
    VulkanHelper
    GIT_REPOSITORY https://github.com/Zydak/VulkanHelper.git
    GIT_TAG 7a590ef81208fea47ce92cb6008dc7b9fadbdfb8
)

FetchContent_MakeAvailable(VulkanHelper)

add_subdirectory(PathTracer)