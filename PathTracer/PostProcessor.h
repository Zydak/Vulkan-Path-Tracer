#pragma once

#include "VulkanHelper.h"

class PostProcessor
{
public:
    struct TonemappingData
    {
        float Exposure = 1.0f;
        float Gamma = 2.2f;
    };

    struct BloomData
    {
    public:
        float BloomThreshold = 2.0f;
        float BloomStrength = 1.1f;
        uint32_t MipCount = 10;
        float FalloffRange = 1.0f;
    };

    PostProcessor() = default;

    static PostProcessor New(VulkanHelper::Device device);

    void SetInputImage(VulkanHelper::ImageView inputImageView);
    void PostProcess(VulkanHelper::CommandBuffer& commandBuffer);
    void SetTonemappingData(const TonemappingData& data, VulkanHelper::CommandBuffer& commandBuffer);
    void SetBloomData(const BloomData& data);

    VulkanHelper::ImageView GetOutputImageView() const { return m_OutputImageView; }

private:
    VulkanHelper::Device m_Device;

    VulkanHelper::ImageView m_InputImageView;
    VulkanHelper::ImageView m_OutputImageView;

    VulkanHelper::DescriptorPool m_DescriptorPool;
    VulkanHelper::DescriptorSet m_TonemappingDescriptorSet;

    VulkanHelper::Sampler m_Sampler;
    
    VulkanHelper::Pipeline m_TonemappingPipeline;
    VulkanHelper::Buffer m_TonemappingBuffer;
    
    std::vector<VulkanHelper::Pipeline> m_BloomDownSamplePipelines;
    std::vector<VulkanHelper::Pipeline> m_BloomUpSamplePipelines;
    std::vector<VulkanHelper::ImageView> m_BloomViews;

    std::vector<VulkanHelper::DescriptorSet> m_BloomDescriptorSets;

    struct BloomPushData
    {
        float BloomThreshold = 2.0f;
        float BloomStrength = 1.1f;
        int FirstBatch = 1;
        float FalloffRange = 1.0f;
    };
    BloomPushData m_BloomPushData{};
    uint32_t m_MipCount = 10;

    VulkanHelper::PushConstant m_BloomPushConstant;

    VulkanHelper::Sampler m_BloomSampler;

    constexpr static uint32_t MAX_BLOOM_LEVELS = 10;
};