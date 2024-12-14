#include <VulkanHelper.h>
#include "Application.h"

int main()
{
	VulkanHelper::WindowInfo windowInfo{};

	windowInfo.Name = "Path Tracer";
	windowInfo.WorkingDirectory = "";
	windowInfo.WindowHeight = 900;
	windowInfo.WindowWidth = 1600;

	std::shared_ptr<VulkanHelper::Window> window = VulkanHelper::InitWindow(windowInfo);

	VulkanHelper::QueryDevicesInfo queryInfo{};
	queryInfo.Window = window;

	queryInfo.EnableRayTracingSupport = true;
	queryInfo.DeviceExtensions =
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
	queryInfo.OptionalExtensions =
	{
		VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME,
		VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME,
		VK_AMD_DEVICE_COHERENT_MEMORY_EXTENSION_NAME
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

	queryInfo.Features = features;
	queryInfo.UseMemoryAddress = true;

	queryInfo.IgnoredMessageIDs =
	{ 
		-602362517, // Small allocation warning, it's caused by ImGui backend so not much I can do about that
		-1277938581, // Small allocation warning
		1413273847, // Memory priority
		-2027362524, // Command Pool reset
	};

	std::vector<VulkanHelper::Device::PhysicalDevice> physicalDevices = VulkanHelper::QueryDevices(queryInfo);

	VulkanHelper::Device::PhysicalDevice finalChoice = physicalDevices[0];
	// Choose any discrete GPU if it's suitable
	for (int i = 0; i < physicalDevices.size(); i++)
	{
		std::string error;
		if (physicalDevices[i].IsSuitable(error))
		{
			if (finalChoice.Discrete == false)
				finalChoice = physicalDevices[i];
		}
	}

	VulkanHelper::InitializationInfo initInfo{};
	initInfo.MaxFramesInFlight = 1;
	initInfo.PhysicalDevice = finalChoice;
	initInfo.Window = window;

	VulkanHelper::Init(initInfo);

	Application* app = new::Application(window);

	app->Run();

	app->Destroy();
}