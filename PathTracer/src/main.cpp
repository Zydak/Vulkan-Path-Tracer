#define VL_ENTRY_POINT

#include <VulkanHelper.h>
#include "Application.h"

// Create VL Entry point definition
VulkanHelper::Application* VulkanHelper::CreateApplication()
{
	VulkanHelper::ApplicationInfo appInfo;
	appInfo.Name = "Path Tracer";
	appInfo.WorkingDirectory = "";
	appInfo.EnableRayTracingSupport = true;
	appInfo.DeviceExtensions = 
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
		VK_KHR_SHADER_CLOCK_EXTENSION_NAME,
		VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
		VK_KHR_RAY_QUERY_EXTENSION_NAME,
		VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
		VK_KHR_EXTERNAL_FENCE_WIN32_EXTENSION_NAME,
	};
	appInfo.OptionalExtensions =
	{
		VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME,
		VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME
	};

	VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	features.features.shaderInt64 = true;

	VkPhysicalDeviceMemoryPriorityFeaturesEXT memoryPriorityFeatures{};
	memoryPriorityFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT;
	memoryPriorityFeatures.memoryPriority = true;

	VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures = {};
	accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	accelerationStructureFeatures.accelerationStructure = true;

	VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingFeatures = {};
	rayTracingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
	rayTracingFeatures.rayTracingPipeline = true;

	VkPhysicalDeviceBufferDeviceAddressFeaturesKHR deviceAddressFeatures = {};
	deviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR;
	deviceAddressFeatures.bufferDeviceAddress = true;

	VkPhysicalDeviceScalarBlockLayoutFeaturesEXT scalarBlockLayoutFeatures = {};
	scalarBlockLayoutFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES_EXT;
	scalarBlockLayoutFeatures.scalarBlockLayout = true;

	VkPhysicalDeviceShaderClockFeaturesKHR shaderClockFeatures = {};
	shaderClockFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR;
	shaderClockFeatures.shaderDeviceClock = true;
	shaderClockFeatures.shaderSubgroupClock = true;

	VkPhysicalDeviceHostQueryResetFeaturesEXT hostQueryResetFeatures = {};
	hostQueryResetFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT;
	hostQueryResetFeatures.hostQueryReset = true;

	VkPhysicalDeviceTimelineSemaphoreFeaturesKHR timelineSemaphoreFeatures = {};
	timelineSemaphoreFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR;
	timelineSemaphoreFeatures.timelineSemaphore = true;

	VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2Features = {};
	synchronization2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
	synchronization2Features.synchronization2 = true;

	VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures = {};
	indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
	indexingFeatures.runtimeDescriptorArray = true;

	VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = {};
	rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
	rayQueryFeatures.rayQuery = true;

	VkPhysicalDeviceRobustness2FeaturesEXT robustFeatures = {};
	robustFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
	robustFeatures.nullDescriptor = true;

	VkPhysicalDeviceVulkan11Features vulkan11Features = {};
	vulkan11Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	vulkan11Features.variablePointers = true;
	vulkan11Features.variablePointersStorageBuffer = true;

	// ------------------------
	// Chain feature structures
	// ------------------------

	features.pNext = &vulkan11Features;
	vulkan11Features.pNext = &robustFeatures;
	robustFeatures.pNext = &indexingFeatures;
	indexingFeatures.pNext = &memoryPriorityFeatures;
	memoryPriorityFeatures.pNext = &accelerationStructureFeatures;
	accelerationStructureFeatures.pNext = &rayTracingFeatures;
	rayTracingFeatures.pNext = &deviceAddressFeatures;
	deviceAddressFeatures.pNext = &scalarBlockLayoutFeatures;
	scalarBlockLayoutFeatures.pNext = &shaderClockFeatures;
	shaderClockFeatures.pNext = &hostQueryResetFeatures;
	hostQueryResetFeatures.pNext = &timelineSemaphoreFeatures;
	timelineSemaphoreFeatures.pNext = &synchronization2Features;
	synchronization2Features.pNext = &rayQueryFeatures;
	rayQueryFeatures.pNext = nullptr;

	appInfo.Features = features;
	appInfo.UseMemoryAddress = true;
	appInfo.WindowHeight = 900;
	appInfo.WindowWidth = 1600;
	appInfo.MaxFramesInFlight = 1;

	Application* app = new ::Application(appInfo);

	vkGetPhysicalDeviceFeatures2(VulkanHelper::Device::GetPhysicalDevice(), &features);

	// Verify that all features are present
	VL_CHECK(accelerationStructureFeatures.accelerationStructure, "acceleration structures not supported!");
	VL_CHECK(rayTracingFeatures.rayTracingPipeline, "Ray Tracing Pipeline not supported!");
	VL_CHECK(deviceAddressFeatures.bufferDeviceAddress, "Device address not supported!");
	VL_CHECK(scalarBlockLayoutFeatures.scalarBlockLayout, "Scalar block layout not supported!");
	VL_CHECK(shaderClockFeatures.shaderDeviceClock, "Shader Clock not supported!");
	VL_CHECK(hostQueryResetFeatures.hostQueryReset, "Host Query not supported!");
	VL_CHECK(timelineSemaphoreFeatures.timelineSemaphore, "Timeline semaphore not supported!");
	VL_CHECK(synchronization2Features.synchronization2, "Synchronization2 not supported!");
	VL_CHECK(indexingFeatures.runtimeDescriptorArray, "Indexing not supported!");
	VL_CHECK(rayQueryFeatures.rayQuery, "Ray query not supported!");
	VL_CHECK(memoryPriorityFeatures.memoryPriority, "memory priority not supported!");
	VL_CHECK(robustFeatures.nullDescriptor, "nullDescriptor not supported!");
	VL_CHECK(vulkan11Features.variablePointers, "variablePointers not supported!");
	VL_CHECK(vulkan11Features.variablePointersStorageBuffer, "variablePointersStorageBuffer not supported!");

	return app;
}