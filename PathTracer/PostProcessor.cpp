#include "PostProcessor.h"

#include <array>

PostProcessor PostProcessor::New(VulkanHelper::Device device)
{
    PostProcessor postProcessor;
    postProcessor.m_Device = device;

    std::array<VulkanHelper::DescriptorPool::PoolSize, 2> poolSizes = {
        VulkanHelper::DescriptorPool::PoolSize{VulkanHelper::DescriptorType::UNIFORM_BUFFER, 10},
        VulkanHelper::DescriptorPool::PoolSize{VulkanHelper::DescriptorType::STORAGE_IMAGE, 10}
    };

    VulkanHelper::DescriptorPool::Config descriptorPoolConfig{};
    descriptorPoolConfig.Device = device;
    descriptorPoolConfig.MaxSets = 100;
    descriptorPoolConfig.PoolSizes = poolSizes.data();
    descriptorPoolConfig.PoolSizeCount = static_cast<uint32_t>(poolSizes.size());

    postProcessor.m_DescriptorPool = VulkanHelper::DescriptorPool::New(descriptorPoolConfig).Value();

    std::array<VulkanHelper::DescriptorSet::BindingDescription, 3> bindingDescriptions = {
        VulkanHelper::DescriptorSet::BindingDescription{0, 1, VulkanHelper::ShaderStages::COMPUTE_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE},
        VulkanHelper::DescriptorSet::BindingDescription{1, 1, VulkanHelper::ShaderStages::COMPUTE_BIT, VulkanHelper::DescriptorType::STORAGE_IMAGE},
        VulkanHelper::DescriptorSet::BindingDescription{2, 1, VulkanHelper::ShaderStages::COMPUTE_BIT, VulkanHelper::DescriptorType::UNIFORM_BUFFER}
    };

    postProcessor.m_TonemappingDescriptorSet = postProcessor.m_DescriptorPool.AllocateDescriptorSet({bindingDescriptions.data(), static_cast<uint32_t>(bindingDescriptions.size())}).Value();

    VulkanHelper::Shader shader = VulkanHelper::Shader::New({
        device,
        "Tonemap.slang",
        VulkanHelper::ShaderStages::COMPUTE_BIT
    }).Value();

    VulkanHelper::Pipeline::ComputeConfig pipelineConfig;
    pipelineConfig.Device = device;
    pipelineConfig.ComputeShader = shader;
    pipelineConfig.DescriptorSets = { postProcessor.m_TonemappingDescriptorSet };

    postProcessor.m_TonemappingPipeline = VulkanHelper::Pipeline::New(pipelineConfig).Value();

    VulkanHelper::Buffer::Config bufferConfig;
    bufferConfig.Device = device;
    bufferConfig.Size = sizeof(TonemappingData);
    bufferConfig.Usage = VulkanHelper::Buffer::Usage::UNIFORM_BUFFER_BIT | VulkanHelper::Buffer::Usage::TRANSFER_DST_BIT;
    bufferConfig.DebugName = "TonemappingBuffer";

    postProcessor.m_TonemappingBuffer = VulkanHelper::Buffer::New(bufferConfig).Value();

    VH_ASSERT(postProcessor.m_TonemappingDescriptorSet.AddBuffer(2, 0, postProcessor.m_TonemappingBuffer) == VulkanHelper::VHResult::OK, "Failed to add tonemapping buffer to descriptor set");

    return postProcessor;
}

void PostProcessor::SetInputImage(const VulkanHelper::ImageView& inputImageView)
{
    m_InputImageView = inputImageView;

    VulkanHelper::Image::Config outputImageConfig{};
    outputImageConfig.Device = m_Device;
    outputImageConfig.Format = VulkanHelper::Format::R8G8B8A8_UNORM;
    outputImageConfig.Usage = VulkanHelper::Image::Usage::STORAGE_BIT | VulkanHelper::Image::Usage::SAMPLED_BIT;
    outputImageConfig.Width = m_InputImageView.GetWidth();
    outputImageConfig.Height = m_InputImageView.GetHeight();

    VulkanHelper::Image outputImage = VulkanHelper::Image::New(outputImageConfig).Value();

    m_OutputImageView = VulkanHelper::ImageView::New({ outputImage, VulkanHelper::ImageView::ViewType::VIEW_2D }).Value();

    VH_ASSERT(m_TonemappingDescriptorSet.AddImage(0, 0, inputImageView, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add input image view to descriptor set");
    VH_ASSERT(m_TonemappingDescriptorSet.AddImage(1, 0, m_OutputImageView, VulkanHelper::Image::Layout::GENERAL) == VulkanHelper::VHResult::OK, "Failed to add output image view to descriptor set");
}

void PostProcessor::PostProcess(VulkanHelper::CommandBuffer& commandBuffer)
{
    m_InputImageView.GetImage().TransitionImageLayout(VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL, commandBuffer);
    m_OutputImageView.GetImage().TransitionImageLayout(VulkanHelper::Image::Layout::GENERAL, commandBuffer);

    m_TonemappingPipeline.Bind(commandBuffer);
    m_TonemappingPipeline.Dispatch(commandBuffer, (uint32_t)glm::ceil((float)m_InputImageView.GetWidth() / (float)32), (uint32_t)glm::ceil((float)m_InputImageView.GetHeight() / (float)32), 1);
}

void PostProcessor::SetTonemappingData(const TonemappingData& data, VulkanHelper::CommandBuffer& commandBuffer)
{
    VH_ASSERT(m_TonemappingBuffer.UploadData(&data, sizeof(data), 0, &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload tonemapping data");
}
