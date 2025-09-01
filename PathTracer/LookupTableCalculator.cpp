#include "LookupTableCalculator.h"

#include <array>
#include <chrono>

LookupTableCalculator LookupTableCalculator::New(VulkanHelper::Device device, const std::string& shaderFilepath, const std::vector<VulkanHelper::Shader::Define>& defines)
{
    VulkanHelper::Shader::InitializeSession("../PathTracer/Shaders/", defines.size(), defines.data());
    LookupTableCalculator calculator;
    calculator.m_Device = device;

    std::array<VulkanHelper::DescriptorPool::PoolSize, 1> poolSizes = {{
        VulkanHelper::DescriptorPool::PoolSize{VulkanHelper::DescriptorType::STORAGE_BUFFER, 1},
    }};

    VulkanHelper::DescriptorPool pool = VulkanHelper::DescriptorPool::New({device, 1, poolSizes.data(), poolSizes.size()}).Value();

    VulkanHelper::DescriptorSet::BindingDescription bindingDescription = {};
    bindingDescription.Binding = 0;
    bindingDescription.Type = VulkanHelper::DescriptorType::STORAGE_BUFFER;
    bindingDescription.DescriptorsCount = 1;
    bindingDescription.StageFlags = VulkanHelper::ShaderStages::COMPUTE_BIT;

    calculator.m_DescriptorSet = pool.AllocateDescriptorSet({&bindingDescription, 1}).Value();

    calculator.m_PushConstant = VulkanHelper::PushConstant::New({
        .Stage = VulkanHelper::ShaderStages::COMPUTE_BIT,
        .Size = sizeof(PipelinePushConstant)
    }).Value();

    VulkanHelper::Shader shader = VulkanHelper::Shader::New({device, shaderFilepath.c_str(), VulkanHelper::ShaderStages::COMPUTE_BIT}).Value();

    VulkanHelper::Pipeline::ComputeConfig pipelineConfig{};
    pipelineConfig.Device = device;
    pipelineConfig.PushConstant = &calculator.m_PushConstant;
    pipelineConfig.ComputeShader = shader;
    pipelineConfig.DescriptorSets.PushBack(calculator.m_DescriptorSet);

    calculator.m_Pipeline = VulkanHelper::Pipeline::New(pipelineConfig).Value();

    return calculator;
}

std::vector<float> LookupTableCalculator::CalculateTable(glm::uvec3 tableSize, uint32_t sampleCount)
{
    VulkanHelper::CommandPool commandPool = VulkanHelper::CommandPool::New({m_Device, VulkanHelper::CommandPool::Flags::RESET_COMMAND_BUFFER_BIT, m_Device.GetQueueFamilyIndices().ComputeFamily}).Value();
    VulkanHelper::CommandBuffer commandBuffer = commandPool.AllocateCommandBuffer({VulkanHelper::CommandBuffer::Level::PRIMARY}).Value();
    VH_ASSERT(commandBuffer.BeginRecording(VulkanHelper::CommandBuffer::Usage::ONE_TIME_SUBMIT_BIT) == VulkanHelper::VHResult::OK, "Failed to begin recording command buffer");

    uint64_t totalSize = uint64_t(tableSize.x * tableSize.y * tableSize.z);
    std::vector<float> result(totalSize, 0.0f);

    uint64_t totalSampleCount = uint64_t(sampleCount * totalSize);
    VH_LOG_DEBUG("Calculating lookup table with Total sample count: {}", totalSampleCount);

    VulkanHelper::Buffer::Config bufferConfig{};
    bufferConfig.Device = m_Device;
    bufferConfig.Size = totalSize * sizeof(float);
    bufferConfig.Usage = VulkanHelper::Buffer::Usage::STORAGE_BUFFER_BIT | VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT | VulkanHelper::Buffer::Usage::TRANSFER_DST_BIT;
    bufferConfig.DebugName = "Lookup table buffer";

    VulkanHelper::Buffer buffer = VulkanHelper::Buffer::New(bufferConfig).Value();

    // Set all values to 0
    VH_ASSERT(buffer.UploadData(result.data(), bufferConfig.Size, 0, &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload data to buffer");
    VH_ASSERT(commandBuffer.EndRecording() == VulkanHelper::VHResult::OK, "Failed to end recording command buffer");
    VH_ASSERT(commandBuffer.SubmitAndWait() == VulkanHelper::VHResult::OK, "Failed to submit command buffer");
    VH_ASSERT(commandBuffer.BeginRecording(VulkanHelper::CommandBuffer::Usage::ONE_TIME_SUBMIT_BIT) == VulkanHelper::VHResult::OK, "Failed to begin recording command buffer");

    // Update descriptor set
    VH_ASSERT(m_DescriptorSet.AddBuffer(0, 0, buffer) == VulkanHelper::VHResult::OK, "Failed to update descriptor set");

    // Set Push data
    PipelinePushConstant pushConstantData{};
    pushConstantData.SampleCount = 20;
    pushConstantData.TableSizeX = tableSize.x;
    pushConstantData.TableSizeY = tableSize.y;
    pushConstantData.TableSizeZ = tableSize.z;

    VH_ASSERT(m_PushConstant.SetData(&pushConstantData, sizeof(pushConstantData), 0) == VulkanHelper::VHResult::OK, "Failed to set push constant data");

    m_Pipeline.Bind(commandBuffer);

    auto PCGHash = [](uint32_t input){
        uint32_t state = input * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    };

    auto timer = std::chrono::high_resolution_clock::now();
    uint32_t loopCount = sampleCount / 20; // Each dispatch takes 20 samples
    for (uint32_t i = 0; i < loopCount; i++)
    {
        float timeMillis = std::chrono::duration<float, std::micro>(std::chrono::high_resolution_clock::now() - timer).count() * 0.001f;
        uint32_t seed = PCGHash(i * 2 + sampleCount + PCGHash((uint32_t)timeMillis));
        VH_ASSERT(m_PushConstant.SetData(&seed, sizeof(uint32_t), offsetof(PipelinePushConstant, Seed)) == VulkanHelper::VHResult::OK, "Failed to set push constant data");
        m_Pipeline.Dispatch(commandBuffer, tableSize.x / 8 + 1, tableSize.y / 8 + 1, tableSize.z);

        buffer.Barrier(
            commandBuffer,
            VulkanHelper::AccessFlags::SHADER_WRITE_BIT | VulkanHelper::AccessFlags::SHADER_READ_BIT,
            VulkanHelper::AccessFlags::SHADER_WRITE_BIT | VulkanHelper::AccessFlags::SHADER_READ_BIT,
            VulkanHelper::PipelineStages::COMPUTE_SHADER_BIT,
            VulkanHelper::PipelineStages::COMPUTE_SHADER_BIT
        );

        // Display percentage every 5%
        if (i % (loopCount / 20) == 0)
        {
            VH_LOG_DEBUG("Progress: {}%", (i + 1) * 100 / loopCount);
        }

        // With some really big sample counts the GPU will stall for some time, and if the GPU is unresponsive for a couple of seconds
	    // vulkan just crashes with VK_DEVICE_LOST, that's supposed to prevent infinite loops and dead locks. So anyway the calculation
	    // has to be broken into multiple calls so that it doesn't stall the GPU for too long, here I end the command buffer every 50 dispatches.
        if (i % 50 == 0 && i != 0)
        {
            VH_ASSERT(commandBuffer.EndRecording() == VulkanHelper::VHResult::OK, "Failed to end recording command buffer");
            VH_ASSERT(commandBuffer.SubmitAndWait() == VulkanHelper::VHResult::OK, "Failed to submit command buffer");
            VH_ASSERT(commandBuffer.BeginRecording(VulkanHelper::CommandBuffer::Usage::ONE_TIME_SUBMIT_BIT) == VulkanHelper::VHResult::OK, "Failed to begin recording command buffer");

            // Rebind stuff
            m_Pipeline.Bind(commandBuffer);
        }
    }

    buffer.Barrier(
        commandBuffer,
        VulkanHelper::AccessFlags::SHADER_WRITE_BIT,
        VulkanHelper::AccessFlags::SHADER_READ_BIT,
        VulkanHelper::PipelineStages::COMPUTE_SHADER_BIT,
        VulkanHelper::PipelineStages::ALL_COMMANDS_BIT
    );

    VH_ASSERT(buffer.DownloadData(result.data(), result.size() * sizeof(float), 0, &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to download data from buffer");

    VH_ASSERT(commandBuffer.EndRecording() == VulkanHelper::VHResult::OK, "Failed to end recording command buffer");
    VH_ASSERT(commandBuffer.SubmitAndWait() == VulkanHelper::VHResult::OK, "Failed to submit command buffer");

    // Normalize all data
    for (size_t i = 0; i < result.size(); i++)
    {
        result[i] /= float(loopCount);
    }

    return result;
}