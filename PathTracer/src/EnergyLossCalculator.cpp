#include "EnergyLossCalculator.h"
#include <glm/gtc/random.hpp>

void EnergyLossCalculator::Init()
{
	if (m_Initialized)
		Destroy();

	VulkanHelper::Shader shaderReflection({ "src/shaders/EnergyLossCalculatorReflection.slang", VK_SHADER_STAGE_COMPUTE_BIT });
	VulkanHelper::Shader shaderRefraction({ "src/shaders/EnergyLossCalculatorRefraction.slang", VK_SHADER_STAGE_COMPUTE_BIT });

	// Descriptor set
	{
		VulkanHelper::DescriptorSetLayout::Binding binding{ 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT };
		m_DescriptorSet.Init(&VulkanHelper::Renderer::GetDescriptorPool(), { binding }, &VulkanHelper::Renderer::GetLinearSampler());

		// Add empty binding
		m_DescriptorSet.AddBuffer(0, { VK_NULL_HANDLE, 0, VK_WHOLE_SIZE });
		m_DescriptorSet.Build();
	}

	// Push Constant
	{
		m_PushConstant.Init({ VK_SHADER_STAGE_COMPUTE_BIT });
	}

	// Pipeline
	{
		VulkanHelper::Pipeline::ComputeCreateInfo info{};
		info.debugName = "Energy loss reflection calculator pipeline";
		info.Shader = &shaderReflection;
		info.DescriptorSetLayouts = { m_DescriptorSet.GetDescriptorSetLayout()->GetDescriptorSetLayoutHandle() };
		info.PushConstants = m_PushConstant.GetRangePtr();
		m_ReflectionPipeline.Init(info);

		info.debugName = "Energy loss refraction calculator pipeline";
		info.Shader = &shaderRefraction;
		m_RefractionPipeline.Init(info);
	}

	m_Initialized = true;
}

void EnergyLossCalculator::Destroy()
{
	Reset();
	m_Initialized = false;
}

void EnergyLossCalculator::Reset()
{

}

std::vector<float> EnergyLossCalculator::CalculateEnergyLossGPU(glm::dvec3 tableSize, uint32_t sampleCount, bool reflection, bool AboveTheSurface)
{
	uint64_t totalSize = tableSize.x * tableSize.y * tableSize.x;
	std::vector<float> dataVec(totalSize);

	uint64_t totalSampleCount = totalSize * uint64_t(sampleCount);
	VL_INFO("Calculating energy compensation LUT with {} total samples, it may take a while.", totalSampleCount);

	// Create Buffer
	VulkanHelper::Buffer dataBuffer;
	{
		VulkanHelper::Buffer::CreateInfo infoBuffer{};
		infoBuffer.InstanceSize = totalSize * sizeof(float);
		infoBuffer.InstanceCount = 1;
		infoBuffer.MemoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		infoBuffer.UsageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

		dataBuffer.Init(infoBuffer);
	}

	// Update descriptor with new buffer
	m_DescriptorSet.UpdateBuffer(0, dataBuffer.DescriptorInfo());

	// Set push data
	m_PushConstant.GetDataPtr()->SampleCount = 20;
	m_PushConstant.GetDataPtr()->TableSizeX = tableSize.x;
	m_PushConstant.GetDataPtr()->TableSizeY = tableSize.y;
	m_PushConstant.GetDataPtr()->TableSizeZ = tableSize.z;
	m_PushConstant.GetDataPtr()->AboveSurface = AboveTheSurface;

	VkCommandBuffer cmdBuffer;
	VulkanHelper::Device::BeginSingleTimeCommands(cmdBuffer, VulkanHelper::Device::GetComputeCommandPool());

	if (reflection)
		m_ReflectionPipeline.Bind(cmdBuffer);
	else
		m_RefractionPipeline.Bind(cmdBuffer);

	// Both layouts are the same so there's no difference from refraction to reflection layout
	m_DescriptorSet.Bind(0, m_ReflectionPipeline.GetPipelineLayout(), VK_PIPELINE_BIND_POINT_COMPUTE, cmdBuffer);
	m_PushConstant.Push(m_ReflectionPipeline.GetPipelineLayout(), cmdBuffer);

	int loopCount = sampleCount / 20;
	for (int i = 0; i < loopCount; i++)
	{
		vkCmdDispatch(cmdBuffer, int(tableSize.x) / 8 + 1, int(tableSize.y) / 8 + 1, int(tableSize.z));

		dataBuffer.Barrier(
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
			VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
			cmdBuffer
		);

		// With some really big sample counts the GPU will stall for some time, and for whatever reason if the GPU is unresponsive for a couple of seconds
		// vulkan just crashes with VK_DEVICE_LOST, I guess that's supposed to prevent some infinite loops and dead locks. So anyway the calculation
		// has to be broken into multiple calls so that it doesn't stall the GPU for too long, here I end the command buffer every 50 dispatches.
		if (i % 50 == 0 && i != 0)
		{
			VulkanHelper::Device::EndSingleTimeCommands(cmdBuffer, VulkanHelper::Device::GetComputeQueue(), VulkanHelper::Device::GetComputeCommandPool());

			VulkanHelper::Device::BeginSingleTimeCommands(cmdBuffer, VulkanHelper::Device::GetComputeCommandPool());

			// Rebind everything
			if (reflection)
				m_ReflectionPipeline.Bind(cmdBuffer);
			else
				m_RefractionPipeline.Bind(cmdBuffer);

			m_DescriptorSet.Bind(0, m_ReflectionPipeline.GetPipelineLayout(), VK_PIPELINE_BIND_POINT_COMPUTE, cmdBuffer);
			m_PushConstant.Push(m_ReflectionPipeline.GetPipelineLayout(), cmdBuffer);
		}
	}

	VulkanHelper::Device::EndSingleTimeCommands(cmdBuffer, VulkanHelper::Device::GetComputeQueue(), VulkanHelper::Device::GetComputeCommandPool());

	dataBuffer.Flush();

	// Set All data to 0
	for (int i = 0; i < dataVec.size(); i++)
	{
		dataVec[i] = 0.0f;
	}

	dataBuffer.ReadFromBuffer(dataVec.data(), totalSize * sizeof(float), 0);

	// Normalize all data
	for (int i = 0; i < dataVec.size(); i++)
	{
		dataVec[i] /= loopCount;
	}

	return dataVec;
}

std::vector<float> EnergyLossCalculator::CalculateReflectionEnergyLossGPU(glm::dvec3 tableSize, uint32_t sampleCount)
{
	return CalculateEnergyLossGPU(tableSize, sampleCount, true, false);
}

std::vector<float> EnergyLossCalculator::CalculateRefractionEnergyLossGPU(glm::dvec3 tableSize, uint32_t sampleCount, bool AboveTheSurface)
{
	return CalculateEnergyLossGPU(tableSize, sampleCount, false, AboveTheSurface);
}