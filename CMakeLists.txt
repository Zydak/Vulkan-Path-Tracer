cmake_minimum_required(VERSION 3.12)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(
    $<$<CONFIG:RelWithDebInfo>:INCLUDE_DEBUG_INFO>
)

add_compile_definitions(
    $<$<CONFIG:Debug>:INCLUDE_DEBUG_INFO>
)

# Set up vcpkg toolchain to work with VulkanHelper's manifest
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(VCPKG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/VulkanHelper/Libraries/vcpkg")
    if(EXISTS "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
        # Set the manifest directory to VulkanHelper
        set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/VulkanHelper" CACHE STRING "Vcpkg manifest directory")
    endif()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")

project(VulkanPathTracer)

include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${PROJECT_SOURCE_DIR}/Libraries")

add_subdirectory(VulkanHelper)
add_subdirectory(PathTracer)