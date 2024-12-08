#include "VulkanHelper.h"

class EnergyLossCalculator
{
public:
	EnergyLossCalculator() { Init(); }

	std::vector<float> CalculateReflectionEnergyLossGPU(glm::uvec3 tableSize, uint32_t sampleCount);
	std::vector<float> CalculateRefractionEnergyLossGPU(glm::uvec3 tableSize, uint32_t sampleCount, bool AboveTheSurface);

	void Init();
	void Destroy();
private:

	std::vector<float> CalculateEnergyLossGPU(glm::uvec3 tableSize, uint32_t sampleCount, bool reflection, bool AboveTheSurface);
private:

	struct PipelinePushConstant
	{
		int SampleCount;
		int TableSizeX;
		int TableSizeY;
		int TableSizeZ;
		bool AboveSurface;
	};

	VulkanHelper::PushConstant<PipelinePushConstant> m_PushConstant;
	VulkanHelper::Pipeline m_ReflectionPipeline;
	VulkanHelper::Pipeline m_RefractionPipeline;
	VulkanHelper::DescriptorSet m_DescriptorSet;

	bool m_Initialized = false;
	void Reset();
};