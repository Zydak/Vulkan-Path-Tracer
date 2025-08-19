#include "VulkanHelper.h"

class LookupTableCalculator
{
public:
    [[nodiscard]] static LookupTableCalculator New(VulkanHelper::Device device, const std::string& shaderFilepath, const std::vector<VulkanHelper::Shader::Define>& defines);

	std::vector<float> CalculateTable(glm::uvec3 tableSize, uint32_t sampleCount);

	void Destroy();
private:
private:

	struct PipelinePushConstant
	{
		uint32_t Seed;

		uint32_t SampleCount;
		uint32_t TableSizeX;
		uint32_t TableSizeY;
		uint32_t TableSizeZ;
	};
	VulkanHelper::Device m_Device;
	VulkanHelper::PushConstant m_PushConstant;
	VulkanHelper::Pipeline m_Pipeline;
	VulkanHelper::DescriptorSet m_DescriptorSet;

	bool m_Initialized = false;
	void Reset();
};