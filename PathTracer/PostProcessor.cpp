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

    // Tonemapping
    {
        std::array<VulkanHelper::DescriptorSet::BindingDescription, 5> bindingDescriptions = {
            VulkanHelper::DescriptorSet::BindingDescription{0, 1, VulkanHelper::ShaderStages::COMPUTE_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE},
            VulkanHelper::DescriptorSet::BindingDescription{1, 1, VulkanHelper::ShaderStages::COMPUTE_BIT, VulkanHelper::DescriptorType::STORAGE_IMAGE},
            VulkanHelper::DescriptorSet::BindingDescription{2, 1, VulkanHelper::ShaderStages::COMPUTE_BIT, VulkanHelper::DescriptorType::UNIFORM_BUFFER},
            VulkanHelper::DescriptorSet::BindingDescription{3, 1, VulkanHelper::ShaderStages::COMPUTE_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // Bloom image
            VulkanHelper::DescriptorSet::BindingDescription{4, 1, VulkanHelper::ShaderStages::COMPUTE_BIT, VulkanHelper::DescriptorType::SAMPLER} // Sampler
        };

        postProcessor.m_TonemappingDescriptorSet = postProcessor.m_DescriptorPool.AllocateDescriptorSet({bindingDescriptions.data(), static_cast<uint32_t>(bindingDescriptions.size())}).Value();

        VulkanHelper::Shader shader = VulkanHelper::Shader::New({
            device,
            "PostProcess/Tonemap.slang",
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

        VH_ASSERT(postProcessor.m_TonemappingDescriptorSet.AddBuffer(2, 0, &postProcessor.m_TonemappingBuffer) == VulkanHelper::VHResult::OK, "Failed to add tonemapping buffer to descriptor set");
    
        postProcessor.m_Sampler = VulkanHelper::Sampler::New({ device }).Value();
        VH_ASSERT(postProcessor.m_TonemappingDescriptorSet.AddSampler(4, 0, &postProcessor.m_Sampler) == VulkanHelper::VHResult::OK, "Failed to add sampler to tonemapping descriptor set");
    }

    // Bloom
    {
        postProcessor.m_BloomSampler = VulkanHelper::Sampler::New({
            device,
            VulkanHelper::Sampler::AddressMode::CLAMP_TO_EDGE,
            VulkanHelper::Sampler::Filter::LINEAR
        }).Value();

        std::array<VulkanHelper::DescriptorSet::BindingDescription, 3> bindingDescriptions = {
            VulkanHelper::DescriptorSet::BindingDescription{0, 1, VulkanHelper::ShaderStages::COMPUTE_BIT, VulkanHelper::DescriptorType::STORAGE_IMAGE},
            VulkanHelper::DescriptorSet::BindingDescription{1, 1, VulkanHelper::ShaderStages::COMPUTE_BIT, VulkanHelper::DescriptorType::STORAGE_IMAGE},
            VulkanHelper::DescriptorSet::BindingDescription{2, 1, VulkanHelper::ShaderStages::COMPUTE_BIT, VulkanHelper::DescriptorType::SAMPLER}
        };

        for (uint32_t i = 0; i < MAX_BLOOM_LEVELS; i++)
        {
            postProcessor.m_BloomDescriptorSets.push_back(postProcessor.m_DescriptorPool.AllocateDescriptorSet({ bindingDescriptions.data(), static_cast<uint32_t>(bindingDescriptions.size()) }).Value());
            VH_ASSERT(postProcessor.m_BloomDescriptorSets[i].AddSampler(2, 0, &postProcessor.m_BloomSampler) == VulkanHelper::VHResult::OK, "Failed to add bloom sampler to descriptor set");
        }

        VulkanHelper::Shader downSampleShader = VulkanHelper::Shader::New({
            device,
            "PostProcess/BloomDownSample.slang",
            VulkanHelper::ShaderStages::COMPUTE_BIT
        }).Value();

        VulkanHelper::Shader upSampleShader = VulkanHelper::Shader::New({
            device,
            "PostProcess/BloomUpSample.slang",
            VulkanHelper::ShaderStages::COMPUTE_BIT
        }).Value();

        postProcessor.m_BloomPushConstant = VulkanHelper::PushConstant::New({
            VulkanHelper::ShaderStages::COMPUTE_BIT,
            nullptr,
            sizeof(BloomPushData)
        }).Value();

        VulkanHelper::Pipeline::ComputeConfig pipelineConfig;
        pipelineConfig.Device = device;
        pipelineConfig.PushConstant = &postProcessor.m_BloomPushConstant;
        
        for (uint32_t i = 0; i < MAX_BLOOM_LEVELS; i++)
        {
            pipelineConfig.DescriptorSets = { postProcessor.m_BloomDescriptorSets[i] };

            pipelineConfig.ComputeShader = downSampleShader;
            postProcessor.m_BloomDownSamplePipelines.push_back(VulkanHelper::Pipeline::New(pipelineConfig).Value());

            pipelineConfig.ComputeShader = upSampleShader;
            postProcessor.m_BloomUpSamplePipelines.push_back(VulkanHelper::Pipeline::New(pipelineConfig).Value());
        }
    }

    return postProcessor;
}

void PostProcessor::SetInputImage(VulkanHelper::ImageView inputImageView)
{
    m_InputImageView = inputImageView;

    // Bloom
    {
        glm::uvec2 currentSize = { m_InputImageView.GetWidth(), m_InputImageView.GetHeight() };
        m_BloomViews.clear();
        m_BloomViews.reserve(10);
        for (uint32_t i = 0; i < MAX_BLOOM_LEVELS; i++)
        {
            VulkanHelper::Image::Config bloomImageConfig{};
            bloomImageConfig.Device = m_Device;
            bloomImageConfig.Format = VulkanHelper::Format::R32G32B32A32_SFLOAT;
            bloomImageConfig.Usage = VulkanHelper::Image::Usage::STORAGE_BIT | VulkanHelper::Image::Usage::SAMPLED_BIT | VulkanHelper::Image::Usage::TRANSFER_SRC_BIT;
            bloomImageConfig.Width = currentSize.x;
            bloomImageConfig.Height = currentSize.y;

            VulkanHelper::Image bloomImage = VulkanHelper::Image::New(bloomImageConfig).Value();
            m_BloomViews.push_back(VulkanHelper::ImageView::New({ bloomImage, VulkanHelper::ImageView::ViewType::VIEW_2D }).Value());

            if (currentSize.x % 2 != 0)
                currentSize.x -= 1;
            if (currentSize.y % 2 != 0)
                currentSize.y -= 1;

            currentSize /= 2;
            if (currentSize.x < 2 || currentSize.y < 2)
                break;
        }

        for (int i = 0; i < (int)m_BloomViews.size(); i++)
        {
            if (i == 0)
            {
                VH_ASSERT(m_BloomDescriptorSets[(size_t)i].AddImage(0, 0, &inputImageView, VulkanHelper::Image::Layout::GENERAL) == VulkanHelper::VHResult::OK, "Failed to add bloom image input view to descriptor set");
            }
            else
            {
                VH_ASSERT(m_BloomDescriptorSets[(size_t)i].AddImage(0, 0, &m_BloomViews[(size_t)glm::max(i - 1, 0)], VulkanHelper::Image::Layout::GENERAL) == VulkanHelper::VHResult::OK, "Failed to add bloom image input view to descriptor set");
            }

            VH_ASSERT(m_BloomDescriptorSets[(size_t)i].AddImage(1, 0, &m_BloomViews[(size_t)i], VulkanHelper::Image::Layout::GENERAL) == VulkanHelper::VHResult::OK, "Failed to add bloom image output view to descriptor set");
        }
    }

    // Tonemapping
    {
        VulkanHelper::Image::Config outputImageConfig{};
        outputImageConfig.Device = m_Device;
        outputImageConfig.Format = VulkanHelper::Format::R8G8B8A8_UNORM;
        outputImageConfig.Usage = VulkanHelper::Image::Usage::STORAGE_BIT | VulkanHelper::Image::Usage::SAMPLED_BIT | VulkanHelper::Image::Usage::TRANSFER_SRC_BIT;
        outputImageConfig.Width = m_InputImageView.GetWidth();
        outputImageConfig.Height = m_InputImageView.GetHeight();

        VulkanHelper::Image outputImage = VulkanHelper::Image::New(outputImageConfig).Value();

        m_OutputImageView = VulkanHelper::ImageView::New({ outputImage, VulkanHelper::ImageView::ViewType::VIEW_2D }).Value();

        VH_ASSERT(m_TonemappingDescriptorSet.AddImage(0, 0, &inputImageView, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add input image view to descriptor set");
        VH_ASSERT(m_TonemappingDescriptorSet.AddImage(1, 0, &m_OutputImageView, VulkanHelper::Image::Layout::GENERAL) == VulkanHelper::VHResult::OK, "Failed to add output image view to descriptor set");
        VH_ASSERT(m_TonemappingDescriptorSet.AddImage(3, 0, &m_BloomViews[0], VulkanHelper::Image::Layout::GENERAL) == VulkanHelper::VHResult::OK, "Failed to add bloom image view to descriptor set");
    }
}

void PostProcessor::PostProcess(VulkanHelper::CommandBuffer& commandBuffer)
{
    m_MipCount = glm::clamp(m_MipCount, 1u, (uint32_t)m_BloomViews.size());

    // Bloom
    {
        m_InputImageView.GetImage().TransitionImageLayout(VulkanHelper::Image::Layout::GENERAL, commandBuffer);

        m_BloomPushData.FirstBatch = 1;
        VH_ASSERT(m_BloomPushConstant.SetData(&m_BloomPushData, sizeof(BloomPushData)) == VulkanHelper::VHResult::OK, "Failed to set bloom push constant data");

        // Down Sample pass
        for (int i = 0; i < (int)m_MipCount; i++)
        {
            m_BloomViews[(size_t)i].GetImage().TransitionImageLayout(VulkanHelper::Image::Layout::GENERAL, commandBuffer);
            m_BloomViews[(size_t)glm::min(i + 1, (int)m_MipCount - 1)].GetImage().TransitionImageLayout(VulkanHelper::Image::Layout::GENERAL, commandBuffer);

            m_BloomDownSamplePipelines[(size_t)i].Bind(commandBuffer);
            glm::uvec2 textureSize;
            if (i == 0)
            {
                textureSize = {m_BloomViews[(size_t)glm::max(i - 1, 0)].GetImage().GetWidth(), m_BloomViews[(size_t)glm::max(i - 1, 0)].GetImage().GetHeight()};
            }
            else
            {
                textureSize = {m_BloomViews[(size_t)glm::max(i, 0)].GetImage().GetWidth(), m_BloomViews[(size_t)glm::max(i, 0)].GetImage().GetHeight()};
            }
            m_BloomDownSamplePipelines[(size_t)i].Dispatch(commandBuffer, (uint32_t)glm::ceil((float)textureSize.x / (float)8), (uint32_t)glm::ceil((float)textureSize.y / (float)8), 1);

            m_BloomViews[(size_t)glm::max(i - 1, 0)].GetImage().Barrier(commandBuffer, 0, 1, VulkanHelper::AccessFlags::SHADER_WRITE_BIT, VulkanHelper::AccessFlags::SHADER_READ_BIT, VulkanHelper::PipelineStages::COMPUTE_SHADER_BIT, VulkanHelper::PipelineStages::COMPUTE_SHADER_BIT);

            m_BloomPushData.FirstBatch = 0;
            VH_ASSERT(m_BloomPushConstant.SetData(&m_BloomPushData, sizeof(m_BloomPushData)) == VulkanHelper::VHResult::OK, "Failed to set bloom push constant data");
        }

        // Up sample pass
        for (int i = (int)m_MipCount - 1; i > 0; i--)
        {
            m_BloomUpSamplePipelines[(size_t)i].Bind(commandBuffer);
            glm::uvec2 textureSize = {m_BloomViews[(size_t)glm::max(i - 1, 0)].GetImage().GetWidth(), m_BloomViews[(size_t)glm::max(i - 1, 0)].GetImage().GetHeight()};
            m_BloomUpSamplePipelines[(size_t)i].Dispatch(commandBuffer, (uint32_t)glm::ceil((float)textureSize.x / (float)8), (uint32_t)glm::ceil((float)textureSize.y / (float)8), 1);
            m_BloomViews[(size_t)glm::max(i - 1, 0)].GetImage().Barrier(commandBuffer, 0, 1, VulkanHelper::AccessFlags::SHADER_WRITE_BIT, VulkanHelper::AccessFlags::SHADER_READ_BIT, VulkanHelper::PipelineStages::COMPUTE_SHADER_BIT, VulkanHelper::PipelineStages::COMPUTE_SHADER_BIT);
        }
    }

    // Tonemap
    {
        m_InputImageView.GetImage().TransitionImageLayout(VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL, commandBuffer);
        m_OutputImageView.GetImage().TransitionImageLayout(VulkanHelper::Image::Layout::GENERAL, commandBuffer);

        m_TonemappingPipeline.Bind(commandBuffer);
        m_TonemappingPipeline.Dispatch(commandBuffer, (uint32_t)glm::ceil((float)m_InputImageView.GetWidth() / (float)8), (uint32_t)glm::ceil((float)m_InputImageView.GetHeight() / (float)8), 1);
    }
}

void PostProcessor::SetTonemappingData(const TonemappingData& data, VulkanHelper::CommandBuffer& commandBuffer)
{
    VH_ASSERT(m_TonemappingBuffer.UploadData(&data, sizeof(data), 0, &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload tonemapping data");
}

void PostProcessor::SetBloomData(const BloomData& data)
{
    m_MipCount = data.MipCount;
    m_BloomPushData.BloomThreshold = data.BloomThreshold;
    m_BloomPushData.BloomStrength = data.BloomStrength;
    m_BloomPushData.FalloffRange = data.FalloffRange;
}