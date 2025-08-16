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

    PostProcessor() = default;

    static PostProcessor New(VulkanHelper::Device device);

    void SetInputImage(const VulkanHelper::ImageView& inputImageView);
    void PostProcess(VulkanHelper::CommandBuffer& commandBuffer);
    void SetTonemappingData(const TonemappingData& data, VulkanHelper::CommandBuffer& commandBuffer);

    VulkanHelper::ImageView GetOutputImageView() const { return m_OutputImageView; }

private:
    VulkanHelper::Device m_Device;

    VulkanHelper::ImageView m_InputImageView;
    VulkanHelper::ImageView m_OutputImageView;

    VulkanHelper::DescriptorPool m_DescriptorPool;

    VulkanHelper::DescriptorSet m_TonemappingDescriptorSet;
    VulkanHelper::Pipeline m_TonemappingPipeline;
    VulkanHelper::Buffer m_TonemappingBuffer;
};