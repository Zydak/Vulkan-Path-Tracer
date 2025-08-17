#include "VulkanHelper.h"

class LookupTableCalculator
{
public:
    [[nodiscard]] static LookupTableCalculator New(VulkanHelper::Device device);

	std::vector<float> CalculateReflectionEnergyLossGPU(glm::uvec3 tableSize, uint32_t sampleCount);
	std::vector<float> CalculateRefractionEnergyLossGPU(glm::uvec3 tableSize, uint32_t sampleCount, bool AboveTheSurface);

	void Destroy();
private:

	std::vector<float> CalculateEnergyLossGPU(glm::uvec3 tableSize, uint32_t sampleCount, bool reflection, bool AboveTheSurface);
private:

	struct PipelinePushConstant
	{
		uint32_t Seed;

		uint32_t SampleCount;
		uint32_t TableSizeX;
		uint32_t TableSizeY;
		uint32_t TableSizeZ;
		bool AboveSurface;
	};
	VulkanHelper::Device m_Device;
	VulkanHelper::PushConstant m_PushConstant;
	VulkanHelper::Pipeline m_ReflectionPipeline;
	VulkanHelper::Pipeline m_RefractionPipeline;
	VulkanHelper::DescriptorSet m_DescriptorSet;

	bool m_Initialized = false;
	void Reset();
};