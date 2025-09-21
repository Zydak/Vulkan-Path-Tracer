file(GLOB_RECURSE SOURCES *.cpp ../Include/*.h)

add_executable(VulkanPathTracer ${SOURCES})

if (NOT MSVC)
    target_compile_options(VulkanPathTracer PRIVATE -Wall -Wextra -Werror -Wpedantic -Wsign-conversion -Wconversion -Wshadow -Wcast-align -Wstrict-overflow=2 -Wlogical-op -Wmissing-declarations)
endif()

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
endif()

add_custom_command(TARGET VulkanPathTracer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../vk_layer_settings.txt" "$<TARGET_FILE_DIR:VulkanPathTracer>"
    )