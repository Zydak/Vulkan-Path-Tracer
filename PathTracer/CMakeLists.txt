file(GLOB_RECURSE SOURCES *.cpp *.h)

add_executable(VulkanPathTracer ${SOURCES})

# Label as SYSTEM to disable warnings in external headers
target_include_directories(VulkanPathTracer SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test/)

# if (NOT MSVC)
#     target_compile_options(VulkanPathTracer PRIVATE -Wall -Wextra -Werror -Wpedantic -Wsign-conversion -Wconversion -Wshadow -Wcast-align -Wstrict-overflow=2 -Wlogical-op -Wmissing-declarations)
# endif()

target_link_libraries(VulkanPathTracer PRIVATE VulkanHelper)

if (WIN32)
    # Copy the DLL to the executable directory
    add_custom_command(TARGET VulkanPathTracer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../VulkanHelper/Libraries/slang/bin/slang.dll" "$<TARGET_FILE_DIR:VulkanPathTracer>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../VulkanHelper/Libraries/slang/bin/slang-glslang.dll" "$<TARGET_FILE_DIR:VulkanPathTracer>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../VulkanHelper/Libraries/slang/bin/slang-rt.dll" "$<TARGET_FILE_DIR:VulkanPathTracer>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../VulkanHelper/Libraries/slang/bin/slang-llvm.dll" "$<TARGET_FILE_DIR:VulkanPathTracer>"
    )
else()
    # On Linux, copy the shared libraries to the executable directory
    add_custom_command(TARGET VulkanPathTracer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../VulkanHelper/Libraries/slang/lib/libslang.so" "$<TARGET_FILE_DIR:VulkanPathTracer>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../VulkanHelper/Libraries/slang/lib/libslang-glslang.so" "$<TARGET_FILE_DIR:VulkanPathTracer>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../VulkanHelper/Libraries/slang/lib/libslang-rt.so" "$<TARGET_FILE_DIR:VulkanPathTracer>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../VulkanHelper/Libraries/slang/lib/libslang-llvm.so" "$<TARGET_FILE_DIR:VulkanPathTracer>"
    )
endif()

#Copy NanoVDB header
add_custom_command(TARGET VulkanPathTracer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/../VulkanHelper/Libraries/openvdb-src-src/nanovdb/nanovdb/PNanoVDB.h" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/PNanoVDB.h"
)

# Copy layer settings
add_custom_command(TARGET VulkanPathTracer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/../vk_layer_settings.txt" "$<TARGET_FILE_DIR:VulkanPathTracer>"
)

# Copy imgui init file
add_custom_command(TARGET VulkanPathTracer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/../imgui.ini" "$<TARGET_FILE_DIR:VulkanPathTracer>"
)