workspace "PathTracer"
    configurations { "Debug", "Release", "Distribution" }
    platforms { "Windows" }

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

globalIncludes = 
{
    "%{wks.location}/Vulkan-Helper/src/VulkanHelper",
    "%{wks.location}/Vulkan-Helper/src/",
    "%{wks.location}",
    "%{wks.location}/Vulkan-Helper/lib/",
    "%{wks.location}/Vulkan-Helper/lib/glfw/include/",
    "%{wks.location}/Vulkan-Helper/lib/imgui/",
    "%{wks.location}/Vulkan-Helper/lib/stbimage/",
    "%{wks.location}/Vulkan-Helper/lib/glm/",
    "%{wks.location}/Vulkan-Helper/lib/msdf-atlas-gen/",
    "%{wks.location}/Vulkan-Helper/lib/msdf-atlas-gen/msdfgen/",
    "%{wks.location}/Vulkan-Helper/lib/shaderc",
    "%{wks.location}/Vulkan-Helper/lib/entt/",
    "%{wks.location}/Vulkan-Helper/lib/Vulkan-Hpp/Vulkan-Headers/include",
    "%{wks.location}/Vulkan-Helper/lib/Vulkan-Hpp/vulkan",
    "%{wks.location}/Vulkan-Helper/lib/vulkanMemoryAllocator/",
    "%{wks.location}/Vulkan-Helper/lib/spdlog/include/",
    "%{wks.location}/Vulkan-Helper/lib/tinyobjloader/",
    "%{wks.location}/Vulkan-Helper/lib/assimp/include/",
    "%{wks.location}/Vulkan-Helper/lib/cuda/include/",
    "%{wks.location}/Vulkan-Helper/lib/Optix/include/",
    "%{wks.location}/Vulkan-Helper/lib/lodepng/",
    "%{wks.location}/Vulkan-Helper/lib/imNodeFlow/include/",
    "%{wks.location}/Vulkan-Helper/lib/slang/include/",
}
	
include "Vulkan-Helper"
include "PathTracer"
