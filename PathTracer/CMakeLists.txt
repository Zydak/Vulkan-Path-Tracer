file(GLOB_RECURSE SOURCES *.cpp ../Include/*.h)

add_executable(VulkanPathTracer ${SOURCES})

if (NOT MSVC)
    target_compile_options(VulkanPathTracer PRIVATE -Wall -Wextra -Werror -Wpedantic -Wsign-conversion -Wconversion -Wshadow -Wcast-align -Wstrict-overflow=2 -Wlogical-op -Wmissing-declarations)
endif()

target_link_libraries(VulkanPathTracer PRIVATE VulkanHelper)