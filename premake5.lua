workspace "PathTracer"
    configurations { "Debug", "Release", "Distribution" }
    platforms { "Windows" }

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

globalIncludes = 
{
    "%{wks.location}/Vulture/src/Vulture",
    "%{wks.location}/Vulture/src/",
    "%{wks.location}",
    "%{wks.location}/Vulture/lib/",
    "%{wks.location}/Vulture/lib/glfw/include/",
    "%{wks.location}/Vulture/lib/imgui/",
    "%{wks.location}/Vulture/lib/stbimage/",
    "%{wks.location}/Vulture/lib/glm/",
    "%{wks.location}/Vulture/lib/msdf-atlas-gen/",
    "%{wks.location}/Vulture/lib/msdf-atlas-gen/msdfgen/",
    "%{wks.location}/Vulture/lib/shaderc",
    "%{wks.location}/Vulture/lib/entt/",
    "%{wks.location}/Vulture/lib/Vulkan-Hpp/Vulkan-Headers/include",
    "%{wks.location}/Vulture/lib/Vulkan-Hpp/vulkan",
    "%{wks.location}/Vulture/lib/vulkanMemoryAllocator/",
    "%{wks.location}/Vulture/lib/spdlog/include/",
    "%{wks.location}/Vulture/lib/tinyobjloader/",
    "%{wks.location}/Vulture/lib/assimp/include/",
    "%{wks.location}/Vulture/lib/cuda/include/",
    "%{wks.location}/Vulture/lib/Optix/include/",
    "%{wks.location}/Vulture/lib/lodepng/",
    "%{wks.location}/Vulture/lib/imNodeFlow/include/",
}
	
include "Vulture"
include "PathTracer"
