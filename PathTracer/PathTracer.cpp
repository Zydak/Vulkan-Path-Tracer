#include "PathTracer.h"

#include <filesystem>
#include <chrono>
#include <array>

PathTracer PathTracer::New(const VulkanHelper::Device& device)
{
    PathTracer pathTracer{};
    pathTracer.m_Device = device;

    pathTracer.m_CommandPool = VulkanHelper::CommandPool::New({
        .Device = device,
        .QueueFamilyIndex = device.GetQueueFamilyIndices().GraphicsFamily
    }).Value();

    // Descriptor pool
    std::array<VulkanHelper::DescriptorPool::PoolSize, 3> poolSizes = {
        VulkanHelper::DescriptorPool::PoolSize{VulkanHelper::DescriptorType::STORAGE_IMAGE, 10},
        VulkanHelper::DescriptorPool::PoolSize{VulkanHelper::DescriptorType::COMBINED_IMAGE_SAMPLER, 10},
        VulkanHelper::DescriptorPool::PoolSize{VulkanHelper::DescriptorType::UNIFORM_BUFFER, 10}
    };

    VulkanHelper::DescriptorPool::Config descriptorPoolConfig{};
    descriptorPoolConfig.Device = device;
    descriptorPoolConfig.MaxSets = 100;
    descriptorPoolConfig.PoolSizes = poolSizes.data();
    descriptorPoolConfig.PoolSizeCount = static_cast<uint32_t>(poolSizes.size());
    pathTracer.m_DescriptorPool = VulkanHelper::DescriptorPool::New(descriptorPoolConfig).Value();

    VulkanHelper::Buffer::Config uniformBufferConfig{};
    uniformBufferConfig.Device = device;
    uniformBufferConfig.Size = sizeof(PathTracerUniform);
    uniformBufferConfig.Usage = VulkanHelper::Buffer::Usage::UNIFORM_BUFFER_BIT | VulkanHelper::Buffer::Usage::TRANSFER_DST_BIT;
    uniformBufferConfig.CpuMapable = true; // For updating uniform data
    pathTracer.m_PathTracerUniformBuffer = VulkanHelper::Buffer::New(uniformBufferConfig).Value();

    VulkanHelper::Buffer::Config materialsBufferConfig{};
    materialsBufferConfig.Device = device;
    materialsBufferConfig.Size = sizeof(VulkanHelper::MaterialAsset) * MAX_ENTITIES;
    materialsBufferConfig.Usage = VulkanHelper::Buffer::Usage::STORAGE_BUFFER_BIT | VulkanHelper::Buffer::Usage::TRANSFER_DST_BIT;
    pathTracer.m_MaterialsBuffer = VulkanHelper::Buffer::New(materialsBufferConfig).Value();

    // Sampler
    VulkanHelper::Sampler::Config samplerConfig{};
    samplerConfig.AddressMode = VulkanHelper::Sampler::AddressMode::REPEAT;
    samplerConfig.MinFilter = VulkanHelper::Sampler::Filter::LINEAR;
    samplerConfig.MagFilter = VulkanHelper::Sampler::Filter::LINEAR;
    samplerConfig.MipmapMode = VulkanHelper::Sampler::MipmapMode::LINEAR;
    samplerConfig.Device = device;

    pathTracer.m_TextureSampler = VulkanHelper::Sampler::New(samplerConfig).Value();
    
    VulkanHelper::Shader::InitializeSession("../../../PathTracer/Shaders/");

    return pathTracer;
}

void PathTracer::PathTrace(VulkanHelper::CommandBuffer& commandBuffer)
{
    static auto timer = std::chrono::high_resolution_clock::now();
    m_OutputImageView.GetImage().TransitionImageLayout(VulkanHelper::Image::Layout::GENERAL, commandBuffer);

    struct Data
    {
        uint32_t FrameCount;
        uint32_t Seed;
    };

    auto PCGHash = [](uint32_t input){
        uint32_t state = input * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    };

    uint32_t timeElapsed = (uint32_t)((uint64_t)std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - timer).count() % UINT32_MAX);

    static uint32_t frameCount = 0;
    Data data;
    data.FrameCount = frameCount++;
    data.Seed = PCGHash(timeElapsed); // Random seed for each frame

    VH_ASSERT(m_PathTracerUniformBuffer.UploadData(&data, sizeof(Data), offsetof(PathTracerUniform, FrameCount), &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload path tracer uniform buffer");

    m_PathTracerPipeline.Bind(commandBuffer);
    m_PathTracerPipeline.RayTrace(commandBuffer, m_OutputImageView.GetImage().GetWidth(), m_OutputImageView.GetImage().GetHeight());
}

void PathTracer::SetScene(const std::string& sceneFilePath)
{
    VulkanHelper::ThreadPool threadPool(4);
    VulkanHelper::AssetImporter importer = VulkanHelper::AssetImporter::New({&threadPool}).Value();
    auto scene = importer.ImportScene(sceneFilePath).get();
    VH_ASSERT(scene.HasValue(), "Failed to import scene! Current working directory: {}, make sure it is correct!", std::filesystem::current_path().string());

    // Load Camera values
    m_AspectRatio = scene.Value().Cameras[0].AspectRatio;
    m_FOV = scene.Value().Cameras[0].FOV;
    glm::mat4 cameraView = scene.Value().Cameras[0].ViewMatrix;

    std::array<VulkanHelper::Format, 3> vertexAttributes = {
        VulkanHelper::Format::R32G32B32_SFLOAT, // Position
        VulkanHelper::Format::R32G32B32_SFLOAT, // Normal
        VulkanHelper::Format::R32G32_SFLOAT, // UV
    };

    VulkanHelper::CommandBuffer initializationCmd = m_CommandPool.AllocateCommandBuffer({VulkanHelper::CommandBuffer::Level::PRIMARY}).Value();
    VH_ASSERT(initializationCmd.BeginRecording(VulkanHelper::CommandBuffer::Usage::ONE_TIME_SUBMIT_BIT) == VulkanHelper::VHResult::OK, "Failed to begin recording initialization command buffer");

    // Meshes
    for (const auto& mesh : scene.Value().Meshes)
    {
        VulkanHelper::Mesh::Config meshConfig{};
        meshConfig.Device = m_Device;
        meshConfig.VertexAttributes = vertexAttributes.data();
        meshConfig.VertexAttributeCount = vertexAttributes.size();
        meshConfig.VertexData = (void*)mesh.Vertices.Data();
        meshConfig.VertexDataSize = mesh.Vertices.Size() * sizeof(VulkanHelper::LoadedMeshVertex);
        meshConfig.IndexData = (void*)mesh.Indices.Data();
        meshConfig.IndexDataSize = mesh.Indices.Size() * sizeof(uint32_t);
        meshConfig.AdditionalUsageFlags = VulkanHelper::Buffer::Usage::SHADER_DEVICE_ADDRESS_BIT | VulkanHelper::Buffer::Usage::ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT | VulkanHelper::Buffer::Usage::STORAGE_BUFFER_BIT;
        meshConfig.CommandBuffer = &initializationCmd;

        m_SceneMeshes.push_back(VulkanHelper::Mesh::New(meshConfig).Value());
    }
    
    // Base Color Textures
    for (const auto& texture : scene.Value().BaseColorTextures)
    {
        VulkanHelper::Image::Config imageConfig{};
        imageConfig.Device = m_Device;
        imageConfig.Width = texture.Width;
        imageConfig.Height = texture.Height;
        imageConfig.Format = VulkanHelper::Format::R8G8B8A8_UNORM;
        imageConfig.Usage = VulkanHelper::Image::Usage::SAMPLED_BIT | VulkanHelper::Image::Usage::TRANSFER_DST_BIT;

        VulkanHelper::Image textureImage = VulkanHelper::Image::New(imageConfig).Value();

        // Upload texture data
        VH_ASSERT(textureImage.UploadData(
            texture.Data.Data(),
            texture.Data.Size() * sizeof(uint8_t),
            0,
            &initializationCmd
        ) == VulkanHelper::VHResult::OK, "Failed to upload texture data");

        VulkanHelper::ImageView::Config imageViewConfig{};
        imageViewConfig.image = textureImage;
        imageViewConfig.ViewType = VulkanHelper::ImageView::ViewType::VIEW_2D;
        imageViewConfig.BaseLayer = 0;
        imageViewConfig.LayerCount = 1;

        m_SceneAlbedoTextures.push_back(VulkanHelper::ImageView::New(imageViewConfig).Value());
    }

    // Normal Textures
    for (const auto& texture : scene.Value().NormalTextures)
    {
        VulkanHelper::Image::Config imageConfig{};
        imageConfig.Device = m_Device;
        imageConfig.Width = texture.Width;
        imageConfig.Height = texture.Height;
        imageConfig.Format = VulkanHelper::Format::R8G8B8A8_UNORM;
        imageConfig.Usage = VulkanHelper::Image::Usage::SAMPLED_BIT | VulkanHelper::Image::Usage::TRANSFER_DST_BIT;

        VulkanHelper::Image textureImage = VulkanHelper::Image::New(imageConfig).Value();

        // Upload texture data
        VH_ASSERT(textureImage.UploadData(
            texture.Data.Data(),
            texture.Data.Size() * sizeof(uint8_t),
            0,
            &initializationCmd
        ) == VulkanHelper::VHResult::OK, "Failed to upload texture data");

        VulkanHelper::ImageView::Config imageViewConfig{};
        imageViewConfig.image = textureImage;
        imageViewConfig.ViewType = VulkanHelper::ImageView::ViewType::VIEW_2D;
        imageViewConfig.BaseLayer = 0;
        imageViewConfig.LayerCount = 1;

        m_SceneNormalTextures.push_back(VulkanHelper::ImageView::New(imageViewConfig).Value());
    }

    // Roughness Textures
    for (const auto& texture : scene.Value().RoughnessTextures)
    {
        VulkanHelper::Image::Config imageConfig{};
        imageConfig.Device = m_Device;
        imageConfig.Width = texture.Width;
        imageConfig.Height = texture.Height;
        imageConfig.Format = VulkanHelper::Format::R8_UNORM;
        imageConfig.Usage = VulkanHelper::Image::Usage::SAMPLED_BIT | VulkanHelper::Image::Usage::TRANSFER_DST_BIT;

        VulkanHelper::Image textureImage = VulkanHelper::Image::New(imageConfig).Value();

        // Pack texture data into a single channel since there's only roughness values
        std::vector<uint8_t> packedData(texture.Data.Size() / 4);
        for (size_t i = 0; i < texture.Data.Size(); i += 4)
        {
            packedData[i / 4] = texture.Data[i]; // Take the R channel
        }

        // Upload texture data
        VH_ASSERT(textureImage.UploadData(
            packedData.data(),
            packedData.size() * sizeof(uint8_t),
            0,
            &initializationCmd
        ) == VulkanHelper::VHResult::OK, "Failed to upload texture data");

        VulkanHelper::ImageView::Config imageViewConfig{};
        imageViewConfig.image = textureImage;
        imageViewConfig.ViewType = VulkanHelper::ImageView::ViewType::VIEW_2D;
        imageViewConfig.BaseLayer = 0;
        imageViewConfig.LayerCount = 1;

        m_SceneRoughnessTextures.push_back(VulkanHelper::ImageView::New(imageViewConfig).Value());
    }

    // Metallic Textures
    for (const auto& texture : scene.Value().MetallicTextures)
    {
        VulkanHelper::Image::Config imageConfig{};
        imageConfig.Device = m_Device;
        imageConfig.Width = texture.Width;
        imageConfig.Height = texture.Height;
        imageConfig.Format = VulkanHelper::Format::R8_UNORM;
        imageConfig.Usage = VulkanHelper::Image::Usage::SAMPLED_BIT | VulkanHelper::Image::Usage::TRANSFER_DST_BIT;

        VulkanHelper::Image textureImage = VulkanHelper::Image::New(imageConfig).Value();

        // Pack texture data into a single channel since there's only metallic values
        std::vector<uint8_t> packedData(texture.Data.Size() / 4);
        for (size_t i = 0; i < texture.Data.Size(); i += 4)
        {
            packedData[i / 4] = texture.Data[i]; // Take the R channel
        }

        // Upload texture data
        VH_ASSERT(textureImage.UploadData(
            packedData.data(),
            packedData.size() * sizeof(uint8_t),
            0,
            &initializationCmd
        ) == VulkanHelper::VHResult::OK, "Failed to upload texture data");

        VulkanHelper::ImageView::Config imageViewConfig{};
        imageViewConfig.image = textureImage;
        imageViewConfig.ViewType = VulkanHelper::ImageView::ViewType::VIEW_2D;
        imageViewConfig.BaseLayer = 0;
        imageViewConfig.LayerCount = 1;

        m_SceneMetallicTextures.push_back(VulkanHelper::ImageView::New(imageViewConfig).Value());
    }

    // Emissive Textures
    for (const auto& texture : scene.Value().EmissiveTextures)
    {
        VulkanHelper::Image::Config imageConfig{};
        imageConfig.Device = m_Device;
        imageConfig.Width = texture.Width;
        imageConfig.Height = texture.Height;
        imageConfig.Format = VulkanHelper::Format::R8G8B8A8_UNORM;
        imageConfig.Usage = VulkanHelper::Image::Usage::SAMPLED_BIT | VulkanHelper::Image::Usage::TRANSFER_DST_BIT;

        VulkanHelper::Image textureImage = VulkanHelper::Image::New(imageConfig).Value();

        // Upload texture data
        VH_ASSERT(textureImage.UploadData(
            texture.Data.Data(),
            texture.Data.Size() * sizeof(uint8_t),
            0,
            &initializationCmd
        ) == VulkanHelper::VHResult::OK, "Failed to upload texture data");

        VulkanHelper::ImageView::Config imageViewConfig{};
        imageViewConfig.image = textureImage;
        imageViewConfig.ViewType = VulkanHelper::ImageView::ViewType::VIEW_2D;
        imageViewConfig.BaseLayer = 0;
        imageViewConfig.LayerCount = 1;

        m_SceneEmissiveTextures.push_back(VulkanHelper::ImageView::New(imageViewConfig).Value());
    }

    // Materials
    std::vector<VulkanHelper::MaterialAsset> materials;
    for (const auto& material : scene.Value().Materials)
    {
        materials.push_back(material);
    }
    VH_ASSERT(m_MaterialsBuffer.UploadData(
        materials.data(),
        materials.size() * sizeof(VulkanHelper::MaterialAsset),
        0,
        &initializationCmd
    ) == VulkanHelper::VHResult::OK, "Failed to upload materials buffer");

    VH_ASSERT(initializationCmd.EndRecording() == VulkanHelper::VHResult::OK, "Failed to end recording initialization command buffer");
    VH_ASSERT(initializationCmd.SubmitAndWait() == VulkanHelper::VHResult::OK, "Failed to submit initialization command buffer");
    VH_ASSERT(initializationCmd.BeginRecording(VulkanHelper::CommandBuffer::Usage::ONE_TIME_SUBMIT_BIT) == VulkanHelper::VHResult::OK, "Failed to begin recording initialization command buffer");

    VulkanHelper::BLAS::Config blasConfig{};
    blasConfig.Device = m_Device;

    blasConfig.VertexBuffers.Reserve(m_SceneMeshes.size());
    for (const auto& mesh : m_SceneMeshes)
        blasConfig.VertexBuffers.PushBack(mesh.GetVertexBuffer());

    blasConfig.IndexBuffers.Reserve(m_SceneMeshes.size());
    for (const auto& mesh : m_SceneMeshes)
        blasConfig.IndexBuffers.PushBack(mesh.GetIndexBuffer());

    blasConfig.VertexSize = sizeof(VulkanHelper::LoadedMeshVertex);
    blasConfig.EnableCompaction = true;
    blasConfig.CommandBuffer = &initializationCmd;

    VulkanHelper::BLAS blas = VulkanHelper::BLAS::New(blasConfig).Value();

    glm::mat4 model = glm::mat4(1.0f);
    m_SceneTLAS = VulkanHelper::TLAS::New({
        m_Device,
        {blas},
        &model,
        &initializationCmd
    }).Value();

    // Create Output Image
    // Size of the output image is based on the Aspect ratio of the camera, so it has to be created when new scene (thus camera) is loaded
    VulkanHelper::Image::Config outputImageConfig{};
    outputImageConfig.Device = m_Device;
    outputImageConfig.Width = (uint32_t)((float)m_ResolutionPixels * m_AspectRatio);
    outputImageConfig.Height = m_ResolutionPixels;
    outputImageConfig.Format = VulkanHelper::Format::R32G32B32A32_SFLOAT;
    outputImageConfig.Usage = VulkanHelper::Image::Usage::STORAGE_BIT | VulkanHelper::Image::Usage::SAMPLED_BIT;

    VulkanHelper::Image outputImage = VulkanHelper::Image::New(outputImageConfig).Value();

    VulkanHelper::ImageView::Config outputImageViewConfig{};
    outputImageViewConfig.image = outputImage;
    outputImageViewConfig.ViewType = VulkanHelper::ImageView::ViewType::VIEW_2D;
    outputImageViewConfig.BaseLayer = 0;
    outputImageViewConfig.LayerCount = 1;

    m_OutputImageView = VulkanHelper::ImageView::New(outputImageViewConfig).Value();

    // Create Descriptor set
    std::array<VulkanHelper::DescriptorSet::BindingDescription, 12> bindingDescriptions = {
        VulkanHelper::DescriptorSet::BindingDescription{0, 1, VulkanHelper::ShaderStages::RAYGEN_BIT, VulkanHelper::DescriptorType::STORAGE_IMAGE},
        VulkanHelper::DescriptorSet::BindingDescription{1, 1, VulkanHelper::ShaderStages::RAYGEN_BIT, VulkanHelper::DescriptorType::ACCELERATION_STRUCTURE_KHR},
        VulkanHelper::DescriptorSet::BindingDescription{2, 1, VulkanHelper::ShaderStages::RAYGEN_BIT, VulkanHelper::DescriptorType::UNIFORM_BUFFER},
        VulkanHelper::DescriptorSet::BindingDescription{3, (uint32_t)m_SceneMeshes.size(), VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::STORAGE_BUFFER}, // vertex Meshes
        VulkanHelper::DescriptorSet::BindingDescription{4, (uint32_t)m_SceneMeshes.size(), VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::STORAGE_BUFFER}, // index Meshes
        VulkanHelper::DescriptorSet::BindingDescription{5, (uint32_t)m_SceneAlbedoTextures.size(), VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // Base Color Textures
        VulkanHelper::DescriptorSet::BindingDescription{6, (uint32_t)m_SceneNormalTextures.size(), VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // Normal Textures
        VulkanHelper::DescriptorSet::BindingDescription{7, (uint32_t)m_SceneRoughnessTextures.size(), VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // Roughness Textures
        VulkanHelper::DescriptorSet::BindingDescription{8, (uint32_t)m_SceneMetallicTextures.size(), VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // Metallic Textures
        VulkanHelper::DescriptorSet::BindingDescription{9, (uint32_t)m_SceneEmissiveTextures.size(), VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // Emissive Textures
        VulkanHelper::DescriptorSet::BindingDescription{10, 1, VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::SAMPLER}, // Sampler
        VulkanHelper::DescriptorSet::BindingDescription{11, 1, VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::STORAGE_BUFFER} // Materials
    };

    VulkanHelper::DescriptorSet::Config descriptorSetConfig{};
    descriptorSetConfig.Bindings = bindingDescriptions.data();
    descriptorSetConfig.BindingCount = static_cast<uint32_t>(bindingDescriptions.size());

    m_PathTracerDescriptorSet = m_DescriptorPool.AllocateDescriptorSet(descriptorSetConfig).Value();
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(0, 0, m_OutputImageView, VulkanHelper::Image::Layout::GENERAL) == VulkanHelper::VHResult::OK, "Failed to add output image view to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddAccelerationStructure(1, 0, m_SceneTLAS) == VulkanHelper::VHResult::OK, "Failed to add TLAS to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(2, 0, m_PathTracerUniformBuffer) == VulkanHelper::VHResult::OK, "Failed to add uniform buffer to descriptor set");
    for (uint32_t i = 0; i < m_SceneMeshes.size(); ++i)
        VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(3, i, m_SceneMeshes[i].GetVertexBuffer()) == VulkanHelper::VHResult::OK, "Failed to add vertex buffer to descriptor set");
    for (uint32_t i = 0; i < m_SceneMeshes.size(); ++i)
        VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(4, i, m_SceneMeshes[i].GetIndexBuffer()) == VulkanHelper::VHResult::OK, "Failed to add index buffer to descriptor set");
    for (uint32_t i = 0; i < m_SceneAlbedoTextures.size(); ++i)
        VH_ASSERT(m_PathTracerDescriptorSet.AddImage(5, i, m_SceneAlbedoTextures[i], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add albedo texture to descriptor set");
    for (uint32_t i = 0; i < m_SceneNormalTextures.size(); ++i)
        VH_ASSERT(m_PathTracerDescriptorSet.AddImage(6, i, m_SceneNormalTextures[i], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add normal texture to descriptor set");
    for (uint32_t i = 0; i < m_SceneRoughnessTextures.size(); ++i)
        VH_ASSERT(m_PathTracerDescriptorSet.AddImage(7, i, m_SceneRoughnessTextures[i], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add roughness texture to descriptor set");
    for (uint32_t i = 0; i < m_SceneMetallicTextures.size(); ++i)
        VH_ASSERT(m_PathTracerDescriptorSet.AddImage(8, i, m_SceneMetallicTextures[i], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add metallic texture to descriptor set");
    for (uint32_t i = 0; i < m_SceneEmissiveTextures.size(); ++i)
        VH_ASSERT(m_PathTracerDescriptorSet.AddImage(9, i, m_SceneEmissiveTextures[i], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add emissive texture to descriptor set");

    VH_ASSERT(m_PathTracerDescriptorSet.AddSampler(10, 0, m_TextureSampler) == VulkanHelper::VHResult::OK, "Failed to add texture sampler to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(11, 0, m_MaterialsBuffer) == VulkanHelper::VHResult::OK, "Failed to add materials buffer to descriptor set");

    // Upload Path Tracer uniform data
    PathTracerUniform pathTracerUniform{};
    glm::mat4 cameraProjection = glm::perspective(glm::radians(m_FOV), m_AspectRatio, 0.1f, 100.0f);
    pathTracerUniform.CameraViewInverse = glm::inverse(cameraView);
    pathTracerUniform.CameraProjectionInverse = glm::inverse(cameraProjection);
    pathTracerUniform.MaxDepth = 20;
    pathTracerUniform.SampleCount = 3;
    pathTracerUniform.MaxLuminance = 500.0f;

    VH_ASSERT(m_PathTracerUniformBuffer.UploadData(&pathTracerUniform, sizeof(PathTracerUniform), 0, &initializationCmd) == VulkanHelper::VHResult::OK, "Failed to upload path tracer uniform data");

    //
    // RT Pipeline
    //

    VulkanHelper::Shader rgenShader = VulkanHelper::Shader::New({m_Device, "RayGen.slang", VulkanHelper::ShaderStages::RAYGEN_BIT}).Value();
    VulkanHelper::Shader hitShader = VulkanHelper::Shader::New({m_Device, "ClosestHit.slang", VulkanHelper::ShaderStages::CLOSEST_HIT_BIT}).Value();
    VulkanHelper::Shader missShader = VulkanHelper::Shader::New({m_Device, "Miss.slang", VulkanHelper::ShaderStages::MISS_BIT}).Value();

    VulkanHelper::Pipeline::RayTracingConfig pipelineConfig{};
    pipelineConfig.Device = m_Device;
    pipelineConfig.RayGenShaders.PushBack(rgenShader);
    pipelineConfig.HitShaders.PushBack(hitShader);
    pipelineConfig.MissShaders.PushBack(missShader);
    pipelineConfig.DescriptorSets.PushBack(m_PathTracerDescriptorSet);
    pipelineConfig.CommandBuffer = &initializationCmd;

    m_PathTracerPipeline = VulkanHelper::Pipeline::New(pipelineConfig).Value();

    VH_ASSERT(initializationCmd.EndRecording() == VulkanHelper::VHResult::OK, "Failed to end recording initialization command buffer");
    VH_ASSERT(initializationCmd.SubmitAndWait() == VulkanHelper::VHResult::OK, "Failed to submit initialization command buffer");
}