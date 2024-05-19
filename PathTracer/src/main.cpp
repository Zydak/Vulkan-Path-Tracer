#define VL_ENTRY_POINT

#include <Vulture.h>
#include "Application.h"

// Create VL Entry point definition
Vulture::Application* Vulture::CreateApplication()
{
	Vulture::ApplicationInfo appInfo;
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

	// ------------------------
	// Chain feature structures
	// ------------------------

	features.pNext = &robustFeatures;
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

	return new ::Application(appInfo, 1600, 900);
}