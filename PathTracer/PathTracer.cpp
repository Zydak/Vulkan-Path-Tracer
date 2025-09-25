#include "PathTracer.h"

#include <filesystem>
#include <chrono>
#include <array>
#include <fstream>
#include <numeric>
#include <numbers>

#include "openvdb/openvdb.h"

PathTracer PathTracer::New(const VulkanHelper::Device& device, VulkanHelper::ThreadPool* threadPool)
{
    openvdb::initialize();

    PathTracer pathTracer{};
    pathTracer.m_Device = device;
    pathTracer.m_ThreadPool = threadPool;

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
    pathTracer.m_PathTracerUniformBuffer = VulkanHelper::Buffer::New(uniformBufferConfig).Value();

    VulkanHelper::Buffer::Config materialsBufferConfig{};
    materialsBufferConfig.Device = device;
    materialsBufferConfig.Size = sizeof(Material) * MAX_ENTITIES;
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

    samplerConfig.AddressMode = VulkanHelper::Sampler::AddressMode::CLAMP_TO_EDGE;
    pathTracer.m_LookupTableSampler = VulkanHelper::Sampler::New(samplerConfig).Value();

    // Volumes buffer
    VulkanHelper::Buffer::Config volumesBufferConfig{};
    volumesBufferConfig.Device = device;
    volumesBufferConfig.Size = sizeof(VolumeGPU) * MAX_ENTITIES;
    volumesBufferConfig.Usage = VulkanHelper::Buffer::Usage::STORAGE_BUFFER_BIT | VulkanHelper::Buffer::Usage::TRANSFER_DST_BIT | VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
    pathTracer.m_VolumesBuffer = VulkanHelper::Buffer::New(volumesBufferConfig).Value();

    if (device.AreRayQueriesSupported())
    {
        pathTracer.m_UseRayQueries = true;
    }
    else
    {
        VH_LOG_WARN("Ray queries are not supported by the current device. Falling back to normal RT pipeline.");
        pathTracer.m_UseRayQueries = false;
    }

    VulkanHelper::PushConstant::Config pushConstantConfig{};
    pushConstantConfig.Stage = VulkanHelper::ShaderStages::RAYGEN_BIT;
    pushConstantConfig.Size = sizeof(PushConstantData);

    pathTracer.m_PathTracerPushConstant = VulkanHelper::PushConstant::New(pushConstantConfig).Value();

    return pathTracer;
}

bool PathTracer::PathTrace(VulkanHelper::CommandBuffer& commandBuffer)
{
    if (m_SamplesAccumulated >= m_MaxSamplesAccumulated)
        return true;

    static auto timer = std::chrono::high_resolution_clock::now();
    m_OutputImageView.GetImage().TransitionImageLayout(VulkanHelper::Image::Layout::GENERAL, commandBuffer);

    auto PCGHash = [](uint32_t input){
        uint32_t state = input * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    };

    uint32_t timeElapsed = (uint32_t)((uint64_t)std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - timer).count() % UINT32_MAX);

    PushConstantData data;
    data.FrameCount = m_FrameCount;
    data.Seed = PCGHash(timeElapsed); // Random seed for each frame
    data.ChunkIndex = m_DispatchCount % (m_ScreenChunkCount * m_ScreenChunkCount);

    VH_ASSERT(m_PathTracerPushConstant.SetData(&data, sizeof(PushConstantData)) == VulkanHelper::VHResult::OK, "Failed to set push constant data");

    m_PathTracerPipeline.Bind(commandBuffer);
    m_PathTracerPipeline.RayTrace(commandBuffer, m_OutputImageView.GetImage().GetWidth() / m_ScreenChunkCount, m_OutputImageView.GetImage().GetHeight() / m_ScreenChunkCount);
    m_DispatchCount++;
    m_FrameCount = (uint32_t)glm::floor((float)m_DispatchCount / (float)(m_ScreenChunkCount * m_ScreenChunkCount));
    m_SamplesAccumulated = (m_FrameCount * m_SamplesPerFrame);

    return false;
}

void PathTracer::SetScene(const std::string& sceneFilePath)
{
    ResetPathTracing();

    m_Volumes.clear();
    m_TotalVertexCount = 0;
    m_TotalIndexCount = 0;

    VulkanHelper::AssetImporter importer = VulkanHelper::AssetImporter::New({m_ThreadPool}).Value();
    auto scene = importer.ImportScene(sceneFilePath).get();
    VH_ASSERT(scene.HasValue(), "Failed to import scene! Current working directory: {}, make sure it is correct!", std::filesystem::current_path().string());

    // Add a default camera if the scene doesn't have any cameras
    if (scene.Value().Cameras.Size() <= 0)
    {
        VulkanHelper::CameraAsset camera;
        camera.AspectRatio = 16.0f / 9.0f;
        camera.FOV = 45.0f;
        camera.ViewMatrix = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        scene.Value().Cameras.PushBack(camera);
    }

    VH_ASSERT(scene.Value().Meshes.Size() > 0, "No meshes found in scene! Please load a scene that contains meshes!");

    // Load Camera values
    const float aspectRatio = scene.Value().Cameras[0].AspectRatio;
    m_CameraViewInverse = glm::inverse(scene.Value().Cameras[0].ViewMatrix);

    std::array<VulkanHelper::Format, 3> vertexAttributes = {
        VulkanHelper::Format::R32G32B32_SFLOAT, // Position
        VulkanHelper::Format::R32G32B32_SFLOAT, // Normal
        VulkanHelper::Format::R32G32_SFLOAT, // UV
    };

    VulkanHelper::CommandBuffer initializationCmd = m_CommandPool.AllocateCommandBuffer({VulkanHelper::CommandBuffer::Level::PRIMARY}).Value();
    VH_ASSERT(initializationCmd.BeginRecording(VulkanHelper::CommandBuffer::Usage::ONE_TIME_SUBMIT_BIT) == VulkanHelper::VHResult::OK, "Failed to begin recording initialization command buffer");

    m_ReflectionLookup = LoadLookupTable("../../Assets/LookupTables/ReflectionLookup.bin", {64, 64, 32}, initializationCmd);
    m_RefractionFromOutsideLookup = LoadLookupTable("../../Assets/LookupTables/RefractionLookupHitFromOutside.bin", {128, 128, 32}, initializationCmd);
    m_RefractionFromInsideLookup = LoadLookupTable("../../Assets/LookupTables/RefractionLookupHitFromInside.bin", {128, 128, 32}, initializationCmd);
    LoadEnvironmentMap(m_EnvMapFilepath.c_str(), initializationCmd);

    // Meshes
    m_SceneMeshes.clear();
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

        m_TotalVertexCount += mesh.Vertices.Size();
        m_TotalIndexCount += mesh.Indices.Size();
    }
    
    // Base Color Textures
    m_SceneBaseColorTextures.clear();
    m_SceneBaseColorTextureNames.clear();
    for (const auto& texture : scene.Value().BaseColorTextures)
    {
        VulkanHelper::Image::Config imageConfig{};
        imageConfig.Device = m_Device;
        imageConfig.Width = texture.Width;
        imageConfig.Height = texture.Height;
        // Base color textures are usually exported in SRGB, so marking this image as SRGB will automatically "linearize" them on sampling in shaders
        // and convert from Linear to SRGB on writing to them
        imageConfig.Format = VulkanHelper::Format::R8G8B8A8_SRGB;
        imageConfig.Usage = VulkanHelper::Image::Usage::SAMPLED_BIT | VulkanHelper::Image::Usage::TRANSFER_DST_BIT;

        VulkanHelper::Image textureImage = VulkanHelper::Image::New(imageConfig).Value();

        textureImage.TransitionImageLayout(VulkanHelper::Image::Layout::TRANSFER_DST_OPTIMAL, initializationCmd, 0, 1);

        // Create a staging buffer
        VulkanHelper::Buffer::Config stagingBufferConfig{};
        stagingBufferConfig.Device = m_Device;
        stagingBufferConfig.Size = texture.Data.Size() * sizeof(uint8_t);
        stagingBufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
        stagingBufferConfig.CpuMapable = true;
        stagingBufferConfig.DebugName = "Texture Staging Buffer";

        VulkanHelper::Buffer stagingBuffer = VulkanHelper::Buffer::New(stagingBufferConfig).Value();
        VH_ASSERT(stagingBuffer.UploadData(texture.Data.Data(), texture.Data.Size() * sizeof(uint8_t), 0) == VulkanHelper::VHResult::OK, "Failed to upload texture data");

        VH_ASSERT(stagingBuffer.CopyToImage(
            initializationCmd,
            textureImage
        ) == VulkanHelper::VHResult::OK, "Failed to copy staging buffer to image");

        textureImage.TransitionImageLayout(VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL, initializationCmd, 0, 1);

        VulkanHelper::ImageView::Config imageViewConfig{};
        imageViewConfig.image = textureImage;
        imageViewConfig.ViewType = VulkanHelper::ImageView::ViewType::VIEW_2D;
        imageViewConfig.BaseLayer = 0;
        imageViewConfig.LayerCount = 1;

        m_SceneBaseColorTextures.push_back(VulkanHelper::ImageView::New(imageViewConfig).Value());
        m_SceneBaseColorTextureNames.push_back(texture.Name);
    }

    // Normal Textures
    m_SceneNormalTextures.clear();
    m_SceneNormalTextureNames.clear();
    for (const auto& texture : scene.Value().NormalTextures)
    {
        VulkanHelper::Image::Config imageConfig{};
        imageConfig.Device = m_Device;
        imageConfig.Width = texture.Width;
        imageConfig.Height = texture.Height;
        imageConfig.Format = VulkanHelper::Format::R8G8B8A8_UNORM;
        imageConfig.Usage = VulkanHelper::Image::Usage::SAMPLED_BIT | VulkanHelper::Image::Usage::TRANSFER_DST_BIT;

        VulkanHelper::Image textureImage = VulkanHelper::Image::New(imageConfig).Value();

        textureImage.TransitionImageLayout(VulkanHelper::Image::Layout::TRANSFER_DST_OPTIMAL, initializationCmd, 0, 1);

        // Create a staging buffer
        VulkanHelper::Buffer::Config stagingBufferConfig{};
        stagingBufferConfig.Device = m_Device;
        stagingBufferConfig.Size = texture.Data.Size() * sizeof(uint8_t);
        stagingBufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
        stagingBufferConfig.CpuMapable = true;
        stagingBufferConfig.DebugName = "Texture Staging Buffer";

        VulkanHelper::Buffer stagingBuffer = VulkanHelper::Buffer::New(stagingBufferConfig).Value();
        VH_ASSERT(stagingBuffer.UploadData(texture.Data.Data(), texture.Data.Size() * sizeof(uint8_t), 0) == VulkanHelper::VHResult::OK, "Failed to upload texture data");

        VH_ASSERT(stagingBuffer.CopyToImage(
            initializationCmd,
            textureImage
        ) == VulkanHelper::VHResult::OK, "Failed to copy staging buffer to image");

        textureImage.TransitionImageLayout(VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL, initializationCmd, 0, 1);

        VulkanHelper::ImageView::Config imageViewConfig{};
        imageViewConfig.image = textureImage;
        imageViewConfig.ViewType = VulkanHelper::ImageView::ViewType::VIEW_2D;
        imageViewConfig.BaseLayer = 0;
        imageViewConfig.LayerCount = 1;

        m_SceneNormalTextures.push_back(VulkanHelper::ImageView::New(imageViewConfig).Value());
        m_SceneNormalTextureNames.push_back(texture.Name);
    }

    // Roughness Textures
    m_SceneRoughnessTextures.clear();
    m_SceneRoughnessTextureNames.clear();
    for (const auto& texture : scene.Value().RoughnessTextures)
    {
        VulkanHelper::Image::Config imageConfig{};
        imageConfig.Device = m_Device;
        imageConfig.Width = texture.Width;
        imageConfig.Height = texture.Height;
        imageConfig.Format = VulkanHelper::Format::R8_UNORM;
        imageConfig.Usage = VulkanHelper::Image::Usage::SAMPLED_BIT | VulkanHelper::Image::Usage::TRANSFER_DST_BIT;

        VulkanHelper::Image textureImage = VulkanHelper::Image::New(imageConfig).Value();

        textureImage.TransitionImageLayout(VulkanHelper::Image::Layout::TRANSFER_DST_OPTIMAL, initializationCmd, 0, 1);

        // Pack texture data into a single channel since there's only roughness values
        std::vector<uint8_t> packedData(texture.Data.Size() / 4);
        for (size_t i = 0; i < texture.Data.Size(); i += 4)
        {
            packedData[i / 4] = texture.Data[i]; // Take the R channel
        }

        // Create a staging buffer
        VulkanHelper::Buffer::Config stagingBufferConfig{};
        stagingBufferConfig.Device = m_Device;
        stagingBufferConfig.Size = texture.Data.Size() * sizeof(uint8_t);
        stagingBufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
        stagingBufferConfig.CpuMapable = true;
        stagingBufferConfig.DebugName = "Texture Staging Buffer";

        VulkanHelper::Buffer stagingBuffer = VulkanHelper::Buffer::New(stagingBufferConfig).Value();
        VH_ASSERT(stagingBuffer.UploadData(texture.Data.Data(), texture.Data.Size() * sizeof(uint8_t), 0) == VulkanHelper::VHResult::OK, "Failed to upload texture data");

        VH_ASSERT(stagingBuffer.CopyToImage(
            initializationCmd,
            textureImage
        ) == VulkanHelper::VHResult::OK, "Failed to copy staging buffer to image");

        textureImage.TransitionImageLayout(VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL, initializationCmd, 0, 1);

        VulkanHelper::ImageView::Config imageViewConfig{};
        imageViewConfig.image = textureImage;
        imageViewConfig.ViewType = VulkanHelper::ImageView::ViewType::VIEW_2D;
        imageViewConfig.BaseLayer = 0;
        imageViewConfig.LayerCount = 1;

        m_SceneRoughnessTextures.push_back(VulkanHelper::ImageView::New(imageViewConfig).Value());
        m_SceneRoughnessTextureNames.push_back(texture.Name);
    }

    // Metallic Textures
    m_SceneMetallicTextures.clear();
    m_SceneMetallicTextureNames.clear();
    for (const auto& texture : scene.Value().MetallicTextures)
    {
        VulkanHelper::Image::Config imageConfig{};
        imageConfig.Device = m_Device;
        imageConfig.Width = texture.Width;
        imageConfig.Height = texture.Height;
        imageConfig.Format = VulkanHelper::Format::R8_UNORM;
        imageConfig.Usage = VulkanHelper::Image::Usage::SAMPLED_BIT | VulkanHelper::Image::Usage::TRANSFER_DST_BIT;

        VulkanHelper::Image textureImage = VulkanHelper::Image::New(imageConfig).Value();

        textureImage.TransitionImageLayout(VulkanHelper::Image::Layout::TRANSFER_DST_OPTIMAL, initializationCmd, 0, 1);

        // Pack texture data into a single channel since there's only metallic values
        std::vector<uint8_t> packedData(texture.Data.Size() / 4);
        for (size_t i = 0; i < texture.Data.Size(); i += 4)
        {
            packedData[i / 4] = texture.Data[i]; // Take the R channel
        }

        // Create a staging buffer
        VulkanHelper::Buffer::Config stagingBufferConfig{};
        stagingBufferConfig.Device = m_Device;
        stagingBufferConfig.Size = texture.Data.Size() * sizeof(uint8_t);
        stagingBufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
        stagingBufferConfig.CpuMapable = true;
        stagingBufferConfig.DebugName = "Texture Staging Buffer";

        VulkanHelper::Buffer stagingBuffer = VulkanHelper::Buffer::New(stagingBufferConfig).Value();
        VH_ASSERT(stagingBuffer.UploadData(texture.Data.Data(), texture.Data.Size() * sizeof(uint8_t), 0) == VulkanHelper::VHResult::OK, "Failed to upload texture data");

        VH_ASSERT(stagingBuffer.CopyToImage(
            initializationCmd,
            textureImage
        ) == VulkanHelper::VHResult::OK, "Failed to copy staging buffer to image");

        textureImage.TransitionImageLayout(VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL, initializationCmd, 0, 1);

        VulkanHelper::ImageView::Config imageViewConfig{};
        imageViewConfig.image = textureImage;
        imageViewConfig.ViewType = VulkanHelper::ImageView::ViewType::VIEW_2D;
        imageViewConfig.BaseLayer = 0;
        imageViewConfig.LayerCount = 1;

        m_SceneMetallicTextures.push_back(VulkanHelper::ImageView::New(imageViewConfig).Value());
        m_SceneMetallicTextureNames.push_back(texture.Name);
    }

    // Emissive Textures
    m_SceneEmissiveTextures.clear();
    m_SceneEmissiveTextureNames.clear();
    for (const auto& texture : scene.Value().EmissiveTextures)
    {
        VulkanHelper::Image::Config imageConfig{};
        imageConfig.Device = m_Device;
        imageConfig.Width = texture.Width;
        imageConfig.Height = texture.Height;
        imageConfig.Format = VulkanHelper::Format::R8G8B8A8_UNORM;
        imageConfig.Usage = VulkanHelper::Image::Usage::SAMPLED_BIT | VulkanHelper::Image::Usage::TRANSFER_DST_BIT;

        VulkanHelper::Image textureImage = VulkanHelper::Image::New(imageConfig).Value();
        textureImage.TransitionImageLayout(VulkanHelper::Image::Layout::TRANSFER_DST_OPTIMAL, initializationCmd, 0, 1);

        // Create a staging buffer
        VulkanHelper::Buffer::Config stagingBufferConfig{};
        stagingBufferConfig.Device = m_Device;
        stagingBufferConfig.Size = texture.Data.Size() * sizeof(uint8_t);
        stagingBufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
        stagingBufferConfig.CpuMapable = true;
        stagingBufferConfig.DebugName = "Texture Staging Buffer";

        VulkanHelper::Buffer stagingBuffer = VulkanHelper::Buffer::New(stagingBufferConfig).Value();
        VH_ASSERT(stagingBuffer.UploadData(texture.Data.Data(), texture.Data.Size() * sizeof(uint8_t), 0) == VulkanHelper::VHResult::OK, "Failed to upload texture data");

        VH_ASSERT(stagingBuffer.CopyToImage(
            initializationCmd,
            textureImage
        ) == VulkanHelper::VHResult::OK, "Failed to copy staging buffer to image");

        textureImage.TransitionImageLayout(VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL, initializationCmd, 0, 1);

        VulkanHelper::ImageView::Config imageViewConfig{};
        imageViewConfig.image = textureImage;
        imageViewConfig.ViewType = VulkanHelper::ImageView::ViewType::VIEW_2D;
        imageViewConfig.BaseLayer = 0;
        imageViewConfig.LayerCount = 1;

        m_SceneEmissiveTextures.push_back(VulkanHelper::ImageView::New(imageViewConfig).Value());
        m_SceneEmissiveTextureNames.push_back(texture.Name);
    }

    // Materials
    m_Materials.clear();
    m_MaterialNames.clear();
    for (const auto& material : scene.Value().Materials)
    {
        Material pathTracerMaterial{};
        pathTracerMaterial.BaseColor = material.BaseColor;
        pathTracerMaterial.EmissiveColor = material.EmissiveColor;
        pathTracerMaterial.SpecularColor = material.SpecularColor;
        pathTracerMaterial.Metallic = material.Metallic;
        pathTracerMaterial.Roughness = material.Roughness;
        pathTracerMaterial.IOR = material.IOR;
        pathTracerMaterial.Transmission = material.Transmission;
        pathTracerMaterial.Anisotropy = material.Anisotropy;
        pathTracerMaterial.AnisotropyRotation = material.AnisotropyRotation;

        m_Materials.push_back(pathTracerMaterial);
        m_MaterialNames.push_back(material.Name);
    }

    // Create a staging buffer to upload materials data
    VulkanHelper::Buffer::Config stagingBufferConfig{};
    stagingBufferConfig.Device = m_Device;
    stagingBufferConfig.Size = sizeof(Material) * m_Materials.size();
    stagingBufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
    stagingBufferConfig.CpuMapable = true;
    VulkanHelper::Buffer stagingBuffer = VulkanHelper::Buffer::New(stagingBufferConfig).Value();

    VH_ASSERT(stagingBuffer.UploadData(
        m_Materials.data(),
        m_Materials.size() * sizeof(Material),
        0
    ) == VulkanHelper::VHResult::OK, "Failed to upload materials buffer");

    VH_ASSERT(m_MaterialsBuffer.CopyFromBuffer(initializationCmd, stagingBuffer, 0, 0, sizeof(Material) * m_Materials.size()) == VulkanHelper::VHResult::OK, "Failed to copy materials buffer");

    VH_ASSERT(initializationCmd.EndRecording() == VulkanHelper::VHResult::OK, "Failed to end recording initialization command buffer");
    VH_ASSERT(initializationCmd.SubmitAndWait() == VulkanHelper::VHResult::OK, "Failed to submit initialization command buffer");
    VH_ASSERT(initializationCmd.BeginRecording(VulkanHelper::CommandBuffer::Usage::ONE_TIME_SUBMIT_BIT) == VulkanHelper::VHResult::OK, "Failed to begin recording initialization command buffer");

    VulkanHelper::Vector<VulkanHelper::BLAS> blasVector;
    blasVector.Reserve(m_SceneMeshes.size());
    VulkanHelper::Vector<glm::mat4> modelMatrices;
    for (const auto& mesh : m_SceneMeshes)
    {
        VulkanHelper::BLAS::Config blasConfig{};
        blasConfig.Device = m_Device;
        blasConfig.CommandBuffer = &initializationCmd;

        blasConfig.VertexBuffers.PushBack(mesh.GetVertexBuffer());
        blasConfig.IndexBuffers.PushBack(mesh.GetIndexBuffer());

        blasConfig.VertexSize = sizeof(VulkanHelper::LoadedMeshVertex);
        blasConfig.EnableCompaction = true;

        blasVector.PushBack(VulkanHelper::BLAS::New(blasConfig).Value());
        modelMatrices.PushBack(glm::mat4(1.0f));
    }

    m_SceneTLAS = VulkanHelper::TLAS::New({
        m_Device,
        blasVector.Clone(),
        modelMatrices.Data(),
        &initializationCmd
    }).Value();

    // Create Output Image
    // Size of the output image is based on the Aspect ratio of the camera, so it has to be created when new scene is loaded
    const int initialRes = 1080;
    m_Width = (uint32_t)((float)initialRes * aspectRatio);
    m_Height = initialRes;
    CreateOutputImageView();

    // Create Descriptor set
    std::array<VulkanHelper::DescriptorSet::BindingDescription, 22> bindingDescriptions = {
        VulkanHelper::DescriptorSet::BindingDescription{0, 1, VulkanHelper::ShaderStages::RAYGEN_BIT, VulkanHelper::DescriptorType::STORAGE_IMAGE},
        VulkanHelper::DescriptorSet::BindingDescription{1, 1, VulkanHelper::ShaderStages::RAYGEN_BIT | VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::ACCELERATION_STRUCTURE_KHR},
        VulkanHelper::DescriptorSet::BindingDescription{2, 1, VulkanHelper::ShaderStages::RAYGEN_BIT | VulkanHelper::ShaderStages::MISS_BIT | VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::UNIFORM_BUFFER},
        VulkanHelper::DescriptorSet::BindingDescription{3, (uint32_t)m_SceneMeshes.size(), VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::STORAGE_BUFFER}, // vertex Meshes
        VulkanHelper::DescriptorSet::BindingDescription{4, (uint32_t)m_SceneMeshes.size(), VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::STORAGE_BUFFER}, // index Meshes
        VulkanHelper::DescriptorSet::BindingDescription{5, (uint32_t)m_SceneBaseColorTextures.size(), VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // Base Color Textures
        VulkanHelper::DescriptorSet::BindingDescription{6, (uint32_t)m_SceneNormalTextures.size(), VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // Normal Textures
        VulkanHelper::DescriptorSet::BindingDescription{7, (uint32_t)m_SceneRoughnessTextures.size(), VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // Roughness Textures
        VulkanHelper::DescriptorSet::BindingDescription{8, (uint32_t)m_SceneMetallicTextures.size(), VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // Metallic Textures
        VulkanHelper::DescriptorSet::BindingDescription{9, (uint32_t)m_SceneEmissiveTextures.size(), VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // Emissive Textures
        VulkanHelper::DescriptorSet::BindingDescription{10, 1, VulkanHelper::ShaderStages::CLOSEST_HIT_BIT | VulkanHelper::ShaderStages::MISS_BIT | VulkanHelper::ShaderStages::RAYGEN_BIT, VulkanHelper::DescriptorType::SAMPLER}, // Sampler
        VulkanHelper::DescriptorSet::BindingDescription{11, 1, VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::STORAGE_BUFFER}, // Materials
        VulkanHelper::DescriptorSet::BindingDescription{12, 1, VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // Reflection Lookup
        VulkanHelper::DescriptorSet::BindingDescription{13, 1, VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // RefractionHitFromOutside Lookup
        VulkanHelper::DescriptorSet::BindingDescription{14, 1, VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // ReflectionHitFromInside Lookup
        VulkanHelper::DescriptorSet::BindingDescription{15, 1, VulkanHelper::ShaderStages::CLOSEST_HIT_BIT | VulkanHelper::ShaderStages::MISS_BIT | VulkanHelper::ShaderStages::RAYGEN_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // Env map
        VulkanHelper::DescriptorSet::BindingDescription{16, 1, VulkanHelper::ShaderStages::CLOSEST_HIT_BIT | VulkanHelper::ShaderStages::RAYGEN_BIT, VulkanHelper::DescriptorType::STORAGE_BUFFER}, // Alias map
        VulkanHelper::DescriptorSet::BindingDescription{17, 1, VulkanHelper::ShaderStages::CLOSEST_HIT_BIT | VulkanHelper::ShaderStages::RAYGEN_BIT, VulkanHelper::DescriptorType::STORAGE_BUFFER}, // Volumes buffer
        VulkanHelper::DescriptorSet::BindingDescription{18, 1, VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::SAMPLER}, // Lookup sampler
        VulkanHelper::DescriptorSet::BindingDescription{19, MAX_HETEROGENEOUS_VOLUMES, VulkanHelper::ShaderStages::CLOSEST_HIT_BIT | VulkanHelper::ShaderStages::RAYGEN_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // Volume density textures
        VulkanHelper::DescriptorSet::BindingDescription{20, MAX_HETEROGENEOUS_VOLUMES, VulkanHelper::ShaderStages::CLOSEST_HIT_BIT | VulkanHelper::ShaderStages::RAYGEN_BIT, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // Volume Temperature textures
        VulkanHelper::DescriptorSet::BindingDescription{21, MAX_HETEROGENEOUS_VOLUMES, VulkanHelper::ShaderStages::CLOSEST_HIT_BIT | VulkanHelper::ShaderStages::RAYGEN_BIT, VulkanHelper::DescriptorType::STORAGE_BUFFER} // Volume max densities buffers
    };

    VulkanHelper::DescriptorSet::Config descriptorSetConfig{};
    descriptorSetConfig.Bindings = bindingDescriptions.data();
    descriptorSetConfig.BindingCount = static_cast<uint32_t>(bindingDescriptions.size());

    m_PathTracerDescriptorSet = m_DescriptorPool.AllocateDescriptorSet(descriptorSetConfig).Value();
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(0, 0, &m_OutputImageView, VulkanHelper::Image::Layout::GENERAL) == VulkanHelper::VHResult::OK, "Failed to add output image view to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddAccelerationStructure(1, 0, &m_SceneTLAS) == VulkanHelper::VHResult::OK, "Failed to add TLAS to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(2, 0, &m_PathTracerUniformBuffer) == VulkanHelper::VHResult::OK, "Failed to add uniform buffer to descriptor set");
    for (uint32_t i = 0; i < m_SceneMeshes.size(); ++i)
    {
        VulkanHelper::Buffer vertexBuffer = m_SceneMeshes[i].GetVertexBuffer();
        VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(3, i, &vertexBuffer) == VulkanHelper::VHResult::OK, "Failed to add vertex buffer to descriptor set");
    }
    for (uint32_t i = 0; i < m_SceneMeshes.size(); ++i)
    {
        VulkanHelper::Buffer indexBuffer = m_SceneMeshes[i].GetIndexBuffer();
        VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(4, i, &indexBuffer) == VulkanHelper::VHResult::OK, "Failed to add index buffer to descriptor set");
    }
    for (uint32_t i = 0; i < m_SceneBaseColorTextures.size(); ++i)
        VH_ASSERT(m_PathTracerDescriptorSet.AddImage(5, i, &m_SceneBaseColorTextures[i], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add albedo texture to descriptor set");
    for (uint32_t i = 0; i < m_SceneNormalTextures.size(); ++i)
        VH_ASSERT(m_PathTracerDescriptorSet.AddImage(6, i, &m_SceneNormalTextures[i], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add normal texture to descriptor set");
    for (uint32_t i = 0; i < m_SceneRoughnessTextures.size(); ++i)
        VH_ASSERT(m_PathTracerDescriptorSet.AddImage(7, i, &m_SceneRoughnessTextures[i], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add roughness texture to descriptor set");
    for (uint32_t i = 0; i < m_SceneMetallicTextures.size(); ++i)
        VH_ASSERT(m_PathTracerDescriptorSet.AddImage(8, i, &m_SceneMetallicTextures[i], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add metallic texture to descriptor set");
    for (uint32_t i = 0; i < m_SceneEmissiveTextures.size(); ++i)
        VH_ASSERT(m_PathTracerDescriptorSet.AddImage(9, i, &m_SceneEmissiveTextures[i], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add emissive texture to descriptor set");

    VH_ASSERT(m_PathTracerDescriptorSet.AddSampler(10, 0, &m_TextureSampler) == VulkanHelper::VHResult::OK, "Failed to add texture sampler to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(11, 0, &m_MaterialsBuffer) == VulkanHelper::VHResult::OK, "Failed to add materials buffer to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(12, 0, &m_ReflectionLookup, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add reflection lookup texture to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(13, 0, &m_RefractionFromOutsideLookup, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add refraction hit from outside lookup texture to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(14, 0, &m_RefractionFromInsideLookup, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add reflection hit from inside lookup texture to descriptor set");

    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(15, 0, &m_EnvMapTexture, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add env map texture to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(16, 0, &m_EnvAliasMap) == VulkanHelper::VHResult::OK, "Failed to add env alias map buffer to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(17, 0, &m_VolumesBuffer) == VulkanHelper::VHResult::OK, "Failed to add volumes buffer to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddSampler(18, 0, &m_LookupTableSampler) == VulkanHelper::VHResult::OK, "Failed to add lookup table sampler to descriptor set");

    // Upload Path Tracer uniform data
    PathTracerUniform pathTracerUniform{};
    m_CameraProjectionInverse = glm::inverse(glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f));
    pathTracerUniform.CameraViewInverse = m_CameraViewInverse;
    pathTracerUniform.CameraProjectionInverse = m_CameraProjectionInverse;
    pathTracerUniform.MaxDepth = m_MaxDepth;
    pathTracerUniform.SampleCount = m_SamplesPerFrame;
    pathTracerUniform.MaxLuminance = m_MaxLuminance;
    pathTracerUniform.FocusDistance = m_FocusDistance;
    pathTracerUniform.DepthOfFieldStrength = m_DepthOfFieldStrength;
    pathTracerUniform.EnvMapRotationAzimuth = m_EnvMapRotationAzimuth;
    pathTracerUniform.EnvMapRotationAltitude = m_EnvMapRotationAltitude;
    pathTracerUniform.VolumesCount = 0; // Starts empty
    pathTracerUniform.EnvironmentIntensity = m_EnvironmentIntensity;
    pathTracerUniform.ScreenChunkCount = m_ScreenChunkCount;

    // Create a staging buffer to upload uniform data
    VulkanHelper::Buffer::Config uniformStagingBufferConfig{};
    uniformStagingBufferConfig.Device = m_Device;
    uniformStagingBufferConfig.Size = sizeof(PathTracerUniform);
    uniformStagingBufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
    uniformStagingBufferConfig.CpuMapable = true;
    VulkanHelper::Buffer uniformStagingBuffer = VulkanHelper::Buffer::New(uniformStagingBufferConfig).Value();

    VH_ASSERT(uniformStagingBuffer.UploadData(&pathTracerUniform, sizeof(PathTracerUniform), 0) == VulkanHelper::VHResult::OK, "Failed to upload path tracer uniform data");
    VH_ASSERT(m_PathTracerUniformBuffer.CopyFromBuffer(initializationCmd, uniformStagingBuffer, 0, 0, sizeof(PathTracerUniform)) == VulkanHelper::VHResult::OK, "Failed to copy path tracer uniform buffer");

    //
    // RT Pipeline
    //

    std::vector<VulkanHelper::Shader::Define> defines;
    if (m_EnableEnvMapMIS)
        defines.push_back({"ENABLE_ENV_MAP_MIS", "1"});
    if (m_ShowEnvMapDirectly)
        defines.push_back({"SHOW_ENV_MAP_DIRECTLY", "1"});
    if (m_Volumes.size() > 0)
        defines.push_back({"ENABLE_VOLUMES", "1"});
    if (m_UseOnlyGeometryNormals)
        defines.push_back({"USE_ONLY_GEOMETRY_NORMALS", "1"});
    if (m_UseEnergyCompensation)
        defines.push_back({"USE_ENERGY_COMPENSATION", "1"});
    if (m_FurnaceTestMode)
        defines.push_back({"FURNACE_TEST_MODE", "1"});
    if (m_UseRayQueries)
        defines.push_back({"USE_RAY_QUERIES", "1"});
    if (m_EnableAtmosphere)
        defines.push_back({"ENABLE_ATMOSPHERE", "1"});

    switch (m_PhaseFunction)
    {
    case PhaseFunction::HENYEY_GREENSTEIN:
        defines.push_back({"PHASE_FUNCTION_HENYEY_GREENSTEIN", "1"});
        break;
    case PhaseFunction::DRAINE:
        defines.push_back({"PHASE_FUNCTION_DRAINE", "1"});
        break;
    case PhaseFunction::HENYEY_GREENSTEIN_PLUS_DRAINE:
        defines.push_back({"PHASE_FUNCTION_HENYEY_GREENSTEIN_PLUS_DRAINE", "1"});
        break;

    default:
        VH_ASSERT(false, "Unknown phase function!");
        break;
    }

    VulkanHelper::Shader::InitializeSession("../../PathTracer/Shaders/", defines.size(), defines.data());
    VulkanHelper::Shader rgenShader = VulkanHelper::Shader::New({m_Device, "RayGen.slang", VulkanHelper::ShaderStages::RAYGEN_BIT}).Value();
    VulkanHelper::Shader hitShader = VulkanHelper::Shader::New({m_Device, "ClosestHit.slang", VulkanHelper::ShaderStages::CLOSEST_HIT_BIT}).Value();
    VulkanHelper::Shader missShader = VulkanHelper::Shader::New({m_Device, "Miss.slang", VulkanHelper::ShaderStages::MISS_BIT}).Value();
    VulkanHelper::Shader shadowMissShader = VulkanHelper::Shader::New({m_Device, "MissShadow.slang", VulkanHelper::ShaderStages::MISS_BIT}).Value();

    VulkanHelper::Pipeline::RayTracingConfig pipelineConfig{};
    pipelineConfig.Device = m_Device;
    pipelineConfig.RayGenShaders.PushBack(rgenShader);
    pipelineConfig.HitShaders.PushBack(hitShader);
    pipelineConfig.MissShaders.PushBack(missShader);
    pipelineConfig.MissShaders.PushBack(shadowMissShader);
    pipelineConfig.DescriptorSets.PushBack(m_PathTracerDescriptorSet);
    pipelineConfig.PushConstant = &m_PathTracerPushConstant;
    pipelineConfig.CommandBuffer = &initializationCmd;

    m_PathTracerPipeline = VulkanHelper::Pipeline::New(pipelineConfig).Value();

    VH_ASSERT(initializationCmd.EndRecording() == VulkanHelper::VHResult::OK, "Failed to end recording initialization command buffer");
    VH_ASSERT(initializationCmd.SubmitAndWait() == VulkanHelper::VHResult::OK, "Failed to submit initialization command buffer");
}

void PathTracer::ResizeImage(uint32_t width, uint32_t height)
{
    m_Width = width;
    m_Height = height;

    // Update the output image view with the new dimensions
    CreateOutputImageView();

    // Update descriptor set
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(0, 0, &m_OutputImageView, VulkanHelper::Image::Layout::GENERAL) == VulkanHelper::VHResult::OK, "Failed to add output image view to descriptor set");

    ResetPathTracing();
}

void PathTracer::CreateOutputImageView()
{
    VulkanHelper::Image::Config outputImageConfig{};
    outputImageConfig.Device = m_Device;
    outputImageConfig.Width = m_Width;
    outputImageConfig.Height = m_Height;
    outputImageConfig.Format = VulkanHelper::Format::R32G32B32A32_SFLOAT;
    outputImageConfig.Usage = VulkanHelper::Image::Usage::STORAGE_BIT | VulkanHelper::Image::Usage::SAMPLED_BIT;

    VulkanHelper::Image outputImage = VulkanHelper::Image::New(outputImageConfig).Value();

    VulkanHelper::ImageView::Config outputImageViewConfig{};
    outputImageViewConfig.image = outputImage;
    outputImageViewConfig.ViewType = VulkanHelper::ImageView::ViewType::VIEW_2D;
    outputImageViewConfig.BaseLayer = 0;
    outputImageViewConfig.LayerCount = 1;

    m_OutputImageView = VulkanHelper::ImageView::New(outputImageViewConfig).Value();
}

void PathTracer::SetMaterial(uint32_t index, const Material& material, VulkanHelper::CommandBuffer commandBuffer)
{
    m_Materials[index] = material;

    // Create a staging buffer to upload material data
    VulkanHelper::Buffer::Config stagingBufferConfig{};
    stagingBufferConfig.Device = m_Device;
    stagingBufferConfig.Size = sizeof(Material);
    stagingBufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
    stagingBufferConfig.CpuMapable = true;
    VulkanHelper::Buffer stagingBuffer = VulkanHelper::Buffer::New(stagingBufferConfig).Value();

    // Update material buffer
    VH_ASSERT(stagingBuffer.UploadData(&material, sizeof(Material), 0) == VulkanHelper::VHResult::OK, "Failed to upload material data");
    VH_ASSERT(m_MaterialsBuffer.CopyFromBuffer(commandBuffer, stagingBuffer, 0, index * sizeof(Material), sizeof(Material)) == VulkanHelper::VHResult::OK, "Failed to copy material buffer");
    ResetPathTracing();
}

VulkanHelper::ImageView PathTracer::LoadTexture(const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer)
{
    VulkanHelper::AssetImporter importer = VulkanHelper::AssetImporter::New({m_ThreadPool}).Value();
    VulkanHelper::TextureAsset textureAsset = importer.ImportTexture(filePath).get().Value();

    VulkanHelper::Image::Config imageConfig{};
    imageConfig.Device = m_Device;
    imageConfig.Width = textureAsset.Width;
    imageConfig.Height = textureAsset.Height;
    imageConfig.Format = VulkanHelper::Format::R8G8B8A8_UNORM;
    imageConfig.Usage = VulkanHelper::Image::Usage::SAMPLED_BIT | VulkanHelper::Image::Usage::TRANSFER_DST_BIT;

    VulkanHelper::Image textureImage = VulkanHelper::Image::New(imageConfig).Value();

    // Create a staging buffer
    VulkanHelper::Buffer::Config stagingBufferConfig{};
    stagingBufferConfig.Device = m_Device;
    stagingBufferConfig.Size = textureAsset.Data.Size() * sizeof(uint8_t);
    stagingBufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
    stagingBufferConfig.CpuMapable = true;
    stagingBufferConfig.DebugName = "Texture Staging Buffer";

    VulkanHelper::Buffer stagingBuffer = VulkanHelper::Buffer::New(stagingBufferConfig).Value();
    VH_ASSERT(stagingBuffer.UploadData(textureAsset.Data.Data(), textureAsset.Data.Size() * sizeof(uint8_t), 0) == VulkanHelper::VHResult::OK, "Failed to upload texture data");

    VH_ASSERT(stagingBuffer.CopyToImage(
        commandBuffer,
        textureImage
    ) == VulkanHelper::VHResult::OK, "Failed to copy staging buffer to image");

    textureImage.TransitionImageLayout(VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL, commandBuffer);

    VulkanHelper::ImageView::Config imageViewConfig{};
    imageViewConfig.image = textureImage;
    imageViewConfig.ViewType = VulkanHelper::ImageView::ViewType::VIEW_2D;
    imageViewConfig.BaseLayer = 0;
    imageViewConfig.LayerCount = 1;

    return VulkanHelper::ImageView::New(imageViewConfig).Value();
}

VulkanHelper::ImageView PathTracer::LoadLookupTable(const char* filepath, glm::uvec3 tableSize, VulkanHelper::CommandBuffer& commandBuffer)
{
    // Reflection
    VulkanHelper::Image::Config imageConfig{};
    imageConfig.Device = m_Device;
    imageConfig.Width = tableSize.x;
    imageConfig.Height = tableSize.y;
    imageConfig.LayerCount = tableSize.z;
    imageConfig.Format = VulkanHelper::Format::R32_SFLOAT;
    imageConfig.Usage = VulkanHelper::Image::Usage::SAMPLED_BIT | VulkanHelper::Image::Usage::TRANSFER_DST_BIT;

    VulkanHelper::Image textureImage = VulkanHelper::Image::New(imageConfig).Value();
    textureImage.TransitionImageLayout(VulkanHelper::Image::Layout::TRANSFER_DST_OPTIMAL, commandBuffer, 0, tableSize.z);

    std::ifstream file(filepath, std::ios::binary);
    VH_ASSERT(file, "Failed to open reflection lookup table");

    // Read the file contents into a buffer
    std::vector<uint8_t> buffer(tableSize.x * tableSize.y * tableSize.z * sizeof(float));
    file.read(reinterpret_cast<char*>(buffer.data()), (std::streamsize)buffer.size());
    file.close();

    // Create a staging buffer
    VulkanHelper::Buffer::Config stagingBufferConfig{};
    stagingBufferConfig.Device = m_Device;
    stagingBufferConfig.Size = tableSize.x * tableSize.y * sizeof(float);
    stagingBufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
    stagingBufferConfig.CpuMapable = true;
    stagingBufferConfig.DebugName = "Lookup Table Staging Buffer";

    VulkanHelper::Buffer stagingBuffer = VulkanHelper::Buffer::New(stagingBufferConfig).Value();

    // Upload texture data one layer at a time
    for (uint64_t i = 0; i < tableSize.z; i++)
    {
        VH_ASSERT(stagingBuffer.UploadData(
            buffer.data() + (i * (uint64_t)tableSize.y * (uint64_t)tableSize.x * (uint64_t)sizeof(float)),
            tableSize.y * tableSize.x * sizeof(float),
            0
        ) == VulkanHelper::VHResult::OK, "Failed to upload texture data");

        VH_ASSERT(stagingBuffer.CopyToImage(
            commandBuffer,
            textureImage,
            0,
            0,
            0,
            tableSize.x,
            tableSize.y,
            i
        ) == VulkanHelper::VHResult::OK, "Failed to copy staging buffer to image");

        // Restart the command buffer
        VH_ASSERT(commandBuffer.EndRecording() == VulkanHelper::VHResult::OK, "Failed to end recording command buffer");
        VH_ASSERT(commandBuffer.SubmitAndWait() == VulkanHelper::VHResult::OK, "Failed to submit command buffer");
        VH_ASSERT(commandBuffer.BeginRecording(VulkanHelper::CommandBuffer::Usage::ONE_TIME_SUBMIT_BIT) == VulkanHelper::VHResult::OK, "Failed to begin recording command buffer");
    }
    textureImage.TransitionImageLayout(VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL, commandBuffer, 0, tableSize.z);

    VulkanHelper::ImageView::Config imageViewConfig{};
    imageViewConfig.image = textureImage;
    imageViewConfig.ViewType = VulkanHelper::ImageView::ViewType::VIEW_2D_ARRAY;
    imageViewConfig.BaseLayer = 0;
    imageViewConfig.LayerCount = tableSize.z;

    return VulkanHelper::ImageView::New(imageViewConfig).Value();
}

void PathTracer::SetBaseColorTexture(uint32_t index, const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer)
{
    if (filePath == "Default White Texture")
    {
        m_SceneBaseColorTextures[index] = LoadDefaultTexture(commandBuffer, false);
        m_SceneBaseColorTextureNames[index] = "Default White Texture";
    }
    else
    {
        m_SceneBaseColorTextures[index] = LoadTexture(filePath.c_str(), commandBuffer);
        m_SceneBaseColorTextureNames[index] = filePath.substr(filePath.find_last_of("/\\") + 1);
    }

    // Update descriptor set
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(5, index, &m_SceneBaseColorTextures[index], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add base color texture to descriptor set");
    ResetPathTracing();
}

void PathTracer::SetNormalTexture(uint32_t index, const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer)
{
    if (filePath == "Default Normal Texture")
    {
        m_SceneNormalTextures[index] = LoadDefaultTexture(commandBuffer, true);
        m_SceneNormalTextureNames[index] = "Default Normal Texture";
    }
    else
    {
        m_SceneNormalTextures[index] = LoadTexture(filePath.c_str(), commandBuffer);
        m_SceneNormalTextureNames[index] = filePath.substr(filePath.find_last_of("/\\") + 1);
    }

    // Update descriptor set
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(6, index, &m_SceneNormalTextures[index], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add normal texture to descriptor set");
    ResetPathTracing();
}

void PathTracer::SetRoughnessTexture(uint32_t index, const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer)
{
    if (filePath == "Default White Texture")
    {
        m_SceneRoughnessTextures[index] = LoadDefaultTexture(commandBuffer, false);
        m_SceneRoughnessTextureNames[index] = "Default White Texture";
    }
    else
    {
        m_SceneRoughnessTextures[index] = LoadTexture(filePath.c_str(), commandBuffer);
        m_SceneRoughnessTextureNames[index] = filePath.substr(filePath.find_last_of("/\\") + 1);
    }

    // Update descriptor set
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(7, index, &m_SceneRoughnessTextures[index], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add roughness texture to descriptor set");
    ResetPathTracing();
}

void PathTracer::SetMetallicTexture(uint32_t index, const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer)
{
    if (filePath == "Default White Texture")
    {
        m_SceneMetallicTextures[index] = LoadDefaultTexture(commandBuffer, false);
        m_SceneMetallicTextureNames[index] = "Default White Texture";
    }
    else
    {
        m_SceneMetallicTextures[index] = LoadTexture(filePath.c_str(), commandBuffer);
        m_SceneMetallicTextureNames[index] = filePath.substr(filePath.find_last_of("/\\") + 1);
    }

    // Update descriptor set
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(8, index, &m_SceneMetallicTextures[index], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add metallic texture to descriptor set");
    ResetPathTracing();
}

void PathTracer::SetEmissiveTexture(uint32_t index, const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer)
{
    if (filePath == "Default White Texture")
    {
        m_SceneEmissiveTextures[index] = LoadDefaultTexture(commandBuffer, false);
        m_SceneEmissiveTextureNames[index] = "Default White Texture";
    }
    else
    {
        m_SceneEmissiveTextures[index] = LoadTexture(filePath.c_str(), commandBuffer);
        m_SceneEmissiveTextureNames[index] = filePath.substr(filePath.find_last_of("/\\") + 1);
    }

    // Update descriptor set
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(9, index, &m_SceneEmissiveTextures[index], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add emissive texture to descriptor set");
    ResetPathTracing();
}

// Creates a staging buffer and uploads data to the uniform buffer using the provided command buffer
void PathTracer::UploadDataToBuffer(VulkanHelper::Buffer buffer, void* data, uint32_t size, uint32_t offset, VulkanHelper::CommandBuffer& commandBuffer)
{
    // Create a staging buffer to upload uniform data
    VulkanHelper::Buffer::Config stagingBufferConfig{};
    stagingBufferConfig.Device = m_Device;
    stagingBufferConfig.Size = size;
    stagingBufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
    stagingBufferConfig.CpuMapable = true;
    VulkanHelper::Buffer stagingBuffer = VulkanHelper::Buffer::New(stagingBufferConfig).Value();

    VH_ASSERT(stagingBuffer.UploadData(data, size, 0) == VulkanHelper::VHResult::OK, "Failed to upload path tracer uniform data");
    VH_ASSERT(buffer.CopyFromBuffer(commandBuffer, stagingBuffer, 0, offset, size) == VulkanHelper::VHResult::OK, "Failed to copy path tracer uniform buffer");
}

void PathTracer::DownloadDataFromBuffer(VulkanHelper::Buffer buffer, void* data, uint32_t size, uint32_t offset, VulkanHelper::CommandBuffer& commandBuffer)
{
    // Create a staging buffer to download uniform data
    VulkanHelper::Buffer::Config stagingBufferConfig{};
    stagingBufferConfig.Device = m_Device;
    stagingBufferConfig.Size = size;
    stagingBufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_DST_BIT;
    stagingBufferConfig.CpuMapable = true;
    VulkanHelper::Buffer stagingBuffer = VulkanHelper::Buffer::New(stagingBufferConfig).Value();

    // Copy data from the uniform buffer to the staging buffer
    VH_ASSERT(stagingBuffer.CopyFromBuffer(commandBuffer, buffer, offset, 0, size) == VulkanHelper::VHResult::OK, "Failed to copy path tracer uniform buffer");
    VH_ASSERT(commandBuffer.EndRecording() == VulkanHelper::VHResult::OK, "Failed to end recording command buffer");
    VH_ASSERT(commandBuffer.SubmitAndWait() == VulkanHelper::VHResult::OK, "Failed to submit command buffer");

    // Restart the command buffer for future use
    VH_ASSERT(commandBuffer.BeginRecording(VulkanHelper::CommandBuffer::Usage::ONE_TIME_SUBMIT_BIT) == VulkanHelper::VHResult::OK, "Failed to begin recording command buffer");

    // Download data from the staging buffer
    VH_ASSERT(stagingBuffer.DownloadData(data, size, 0) == VulkanHelper::VHResult::OK, "Failed to download path tracer uniform data");
}

void PathTracer::SetMaxDepth(uint32_t maxDepth, VulkanHelper::CommandBuffer commandBuffer)
{
    m_MaxDepth = maxDepth;
    UploadDataToBuffer(m_PathTracerUniformBuffer, &maxDepth, sizeof(uint32_t), offsetof(PathTracerUniform, MaxDepth), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetMaxSamplesAccumulated(uint32_t maxSamples)
{
    m_MaxSamplesAccumulated = maxSamples;
}

void PathTracer::SetSamplesPerFrame(uint32_t samplesPerFrame, VulkanHelper::CommandBuffer commandBuffer)
{
    m_SamplesPerFrame = samplesPerFrame;
    UploadDataToBuffer(m_PathTracerUniformBuffer, &samplesPerFrame, sizeof(uint32_t), offsetof(PathTracerUniform, SampleCount), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetMaxLuminance(float maxLuminance, VulkanHelper::CommandBuffer commandBuffer)
{
    m_MaxLuminance = maxLuminance;
    UploadDataToBuffer(m_PathTracerUniformBuffer, &maxLuminance, sizeof(float), offsetof(PathTracerUniform, MaxLuminance), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetFocusDistance(float focusDistance, VulkanHelper::CommandBuffer commandBuffer)
{
    m_FocusDistance = focusDistance;
    UploadDataToBuffer(m_PathTracerUniformBuffer, &focusDistance, sizeof(float), offsetof(PathTracerUniform, FocusDistance), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetDepthOfFieldStrength(float depthOfFieldStrength, VulkanHelper::CommandBuffer commandBuffer)
{
    m_DepthOfFieldStrength = depthOfFieldStrength;
    UploadDataToBuffer(m_PathTracerUniformBuffer, &depthOfFieldStrength, sizeof(float), offsetof(PathTracerUniform, DepthOfFieldStrength), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetEnvMapFilepath(const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer)
{
    m_EnvMapFilepath = filePath;

    LoadEnvironmentMap(filePath, commandBuffer);
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(15, 0, &m_EnvMapTexture, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add env map texture to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(16, 0, &m_EnvAliasMap) == VulkanHelper::VHResult::OK, "Failed to add env alias map buffer to descriptor set");
    ResetPathTracing();
}

void PathTracer::SetEnvMapAzimuth(float azimuth, VulkanHelper::CommandBuffer commandBuffer)
{
    m_EnvMapRotationAzimuth = azimuth;
    UploadDataToBuffer(m_PathTracerUniformBuffer, &azimuth, sizeof(float), offsetof(PathTracerUniform, EnvMapRotationAzimuth), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetEnvMapAltitude(float altitude, VulkanHelper::CommandBuffer commandBuffer)
{
    m_EnvMapRotationAltitude = altitude;
    UploadDataToBuffer(m_PathTracerUniformBuffer, &altitude, sizeof(float), offsetof(PathTracerUniform, EnvMapRotationAltitude), commandBuffer);
    ResetPathTracing();
}

void PathTracer::ReloadShaders(VulkanHelper::CommandBuffer& commandBuffer)
{
    std::vector<VulkanHelper::Shader::Define> defines;

    if (m_EnableEnvMapMIS)
        defines.push_back({"ENABLE_ENV_MAP_MIS", "1"});
    if (m_ShowEnvMapDirectly)
        defines.push_back({"SHOW_ENV_MAP_DIRECTLY", "1"});
    if (m_Volumes.size() > 0)
        defines.push_back({"ENABLE_VOLUMES", "1"});
    if (m_UseOnlyGeometryNormals)
        defines.push_back({"USE_ONLY_GEOMETRY_NORMALS", "1"});
    if (m_UseEnergyCompensation)
        defines.push_back({"USE_ENERGY_COMPENSATION", "1"});
    if (m_FurnaceTestMode)
        defines.push_back({"FURNACE_TEST_MODE", "1"});
    if (m_UseRayQueries)
        defines.push_back({"USE_RAY_QUERIES", "1"});
    if (m_EnableAtmosphere)
        defines.push_back({"ENABLE_ATMOSPHERE", "1"});

    switch (m_PhaseFunction)
    {
    case PhaseFunction::HENYEY_GREENSTEIN:
        defines.push_back({"PHASE_FUNCTION_HENYEY_GREENSTEIN", "1"});
        break;
    case PhaseFunction::DRAINE:
        defines.push_back({"PHASE_FUNCTION_DRAINE", "1"});
        break;
    case PhaseFunction::HENYEY_GREENSTEIN_PLUS_DRAINE:
        defines.push_back({"PHASE_FUNCTION_HENYEY_GREENSTEIN_PLUS_DRAINE", "1"});
        break;

    default:
        VH_ASSERT(false, "Unknown phase function!");
        break;
    }

    VulkanHelper::Shader::InitializeSession("../../PathTracer/Shaders/", defines.size(), defines.data());
    auto rgenShaderRes = VulkanHelper::Shader::New({m_Device, "RayGen.slang", VulkanHelper::ShaderStages::RAYGEN_BIT});
    auto hitShaderRes = VulkanHelper::Shader::New({m_Device, "ClosestHit.slang", VulkanHelper::ShaderStages::CLOSEST_HIT_BIT});
    auto missShaderRes = VulkanHelper::Shader::New({m_Device, "Miss.slang", VulkanHelper::ShaderStages::MISS_BIT});
    auto shadowMissShaderRes = VulkanHelper::Shader::New({m_Device, "MissShadow.slang", VulkanHelper::ShaderStages::MISS_BIT});

    if (!rgenShaderRes.HasValue() || !hitShaderRes.HasValue() || !missShaderRes.HasValue() || !shadowMissShaderRes.HasValue())
    {
        return;
    }

    VulkanHelper::Pipeline::RayTracingConfig pipelineConfig{};
    pipelineConfig.Device = m_Device;
    pipelineConfig.RayGenShaders.PushBack(rgenShaderRes.Value());
    pipelineConfig.HitShaders.PushBack(hitShaderRes.Value());
    pipelineConfig.MissShaders.PushBack(missShaderRes.Value());
    pipelineConfig.MissShaders.PushBack(shadowMissShaderRes.Value());
    pipelineConfig.DescriptorSets.PushBack(m_PathTracerDescriptorSet);
    pipelineConfig.PushConstant = &m_PathTracerPushConstant;
    pipelineConfig.CommandBuffer = &commandBuffer;

    m_PathTracerPipeline = VulkanHelper::Pipeline::New(pipelineConfig).Value();
    ResetPathTracing();
}

void PathTracer::LoadEnvironmentMap(const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer)
{
    VulkanHelper::AssetImporter importer = VulkanHelper::AssetImporter::New({m_ThreadPool}).Value();
    VulkanHelper::TextureAsset textureAsset = importer.ImportTexture(filePath).get().Value();

    VulkanHelper::Image::Config imageConfig{};
    imageConfig.Device = m_Device;
    imageConfig.Width = textureAsset.Width;
    imageConfig.Height = textureAsset.Height;
    imageConfig.Format = VulkanHelper::Format::R32G32B32A32_SFLOAT;
    imageConfig.Usage = VulkanHelper::Image::Usage::SAMPLED_BIT | VulkanHelper::Image::Usage::TRANSFER_DST_BIT;

    VulkanHelper::Image textureImage = VulkanHelper::Image::New(imageConfig).Value();

    textureImage.TransitionImageLayout(VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL, commandBuffer);

    VulkanHelper::ImageView::Config imageViewConfig{};
    imageViewConfig.image = textureImage;
    imageViewConfig.ViewType = VulkanHelper::ImageView::ViewType::VIEW_2D;
    imageViewConfig.BaseLayer = 0;
    imageViewConfig.LayerCount = 1;

    m_EnvMapTexture = VulkanHelper::ImageView::New(imageViewConfig).Value();

    const uint32_t width = textureAsset.Width;
    const uint32_t height = textureAsset.Height;
    const uint64_t size = width * height;
    float* pixels = (float*)textureAsset.Data.Data();

    // Create Importance Buffer for Importance Sampling
    struct AliasMapEntry
    {
        uint32_t Alias; // Alias pointing to another texel
        float Importance; // Importance of the current texel
    };

    std::vector<AliasMapEntry> importanceBuffer(width * height);
    std::vector<float> importanceData(width * height);

    float cosTheta0 = 1.0F; // cosine of the up vector
	const float stepPhi = (float)2.0F * (float)std::numbers::pi / (float)width; // azimuth step
	const float stepTheta = (float)std::numbers::pi / (float)height; // altitude step

    // For each texel of the environment map, compute its solid angle on the unit sphere
    // Then store its energy contribution in 'importanceData',
    // approximated as solid angle * max(R, G, B).
    for (uint32_t y = 0; y < height; ++y)
	{
		const float theta1 = (float)(y + 1) * stepTheta; // altitude angle of currently sampled texel
		const float cosTheta1 = glm::cos(theta1);

		const float area = (cosTheta0 - cosTheta1) * stepPhi;  // solid angle
		cosTheta0 = cosTheta1; // set cosine of the up vector to the altitude cosine to advance the loop

		for (uint32_t x = 0; x < width; ++x)
		{
			const uint32_t idx = y * width + x;
			const uint32_t idx4 = idx * 4; // texel index
            
			// Store the importance of the texel into importance array, importance will be higher for brighter texels
			importanceData[idx] = area * glm::max(pixels[idx4], glm::max(pixels[idx4 + 1], pixels[idx4 + 2]));
		}
	}

    // Creating Alias Map
    //
    // The alias map allows efficient sampling of texels from the environment map
    // based on their importance. The goal is to ensure each texel is selected
    // with probability proportional to its contribution.
    //
    // To build the alias map, we group texels so each group has approximately the same total importance.
    // Lower importance texels are paired with higher importance ones ("aliases")
    // to balance the sampling distribution.
    std::vector<AliasMapEntry> aliasMap(size);

	// Compute the total importance of the environment map.
    // Each entry in importanceData is already weighted by the texel's solid angle,
    // so we simply sum them to get the total unnormalized importance.
    float sum = std::accumulate(importanceData.begin(), importanceData.end(), 0.0f);

    // Normalize the importance values so their average becomes 1.
    // This is required for building the alias map, which assumes
    // all values are scaled relative to the average.
    float average = sum / float(size);
    for (uint32_t i = 0; i < size; i++)
    {
        if (average == 0.0f)
            aliasMap[i].Importance = 0.0f;
        else
            aliasMap[i].Importance = importanceData[i] / average;

        // Initialize the alias index to self
        aliasMap[i].Alias = i;
    }

    // Partition the texels according to their normalized importance.
    // Texels with Importance < 1 (i.e. below the average) are added from the beginning of the array,
    // and texels with Importance ≥ 1 (i.e. above the average) are added from the end.
    //
    // This separates the texels into two groups:
    // - "low energy" texels: below average importance
    // - "high energy" texels: above average importance
	std::vector<uint32_t> partitionTable(size);
	uint32_t              lowEnergyCounter = 0U;
	uint32_t              highEnergyCounter = size;
	for (uint32_t i = 0; i < size; ++i)
	{
		if (aliasMap[i].Importance < 1.F)
		{
			lowEnergyCounter++;
			partitionTable[lowEnergyCounter] = i;
		}
		else
		{
			highEnergyCounter--;
			partitionTable[highEnergyCounter] = i;
		}
	}

    // Associate low energy texels (Importance < 1) with high energy texels (Importance > 1).
    // The alias map requires that each entry represents a group with total normalized importance of 1,
    // so we pair low importance texels with high importance ones to balance them.
    //
    // A single high energy texel may compensate for several low energy ones.
    // For each pairing, we subtract the "missing" importance (1 - low energy) from the high energy texel.
    // Once an high energy texel's importance drops below 1, it's fully used and we move to the next one.
	for (lowEnergyCounter = 0; lowEnergyCounter < highEnergyCounter && highEnergyCounter < size; lowEnergyCounter++)
	{
		const uint32_t lowEnergyIndex = partitionTable[lowEnergyCounter];
		const uint32_t highEnergyIndex = partitionTable[highEnergyCounter];

		// Associate the low energy texel to its higher energy alias
		aliasMap[lowEnergyIndex].Alias = highEnergyIndex;

		// Compute the amount needed to bring the low energy texel up to a normalized importance of 1
		const float differenceWithAverage = 1.F - aliasMap[lowEnergyIndex].Importance;

		// Subtract this amount from the high energy texel
		aliasMap[highEnergyIndex].Importance -= differenceWithAverage;

		// If the combined ratio to average of the high energy texel reaches 1, a balance has been found
		// between a set of low energy texels and the high energy one. In this case, move to the next high energy texel
		if (aliasMap[highEnergyIndex].Importance < 1.0f)
		{
			highEnergyCounter++;
		}
	}

    // Compute normalized importance weights for each texel based on a brightness.
    // These normalized values approximate a discrete PDF over the environment map,
    // and are stored in the alpha channel (pixels[idx4 + 3]) for use in shaders.
	for (uint32_t i = 0; i < width * height; ++i)
	{
		const uint32_t idx4 = i * 4;
		// Store the PDF inside Alpha channel(idx4 + 3)
        if (sum == 0.0f)
            pixels[idx4 + 3] = 0.0f;
        else
		    pixels[idx4 + 3] = glm::max(pixels[idx4], glm::max(pixels[idx4 + 1], pixels[idx4 + 2])) / sum;
	}

    // Create a staging buffer
    VulkanHelper::Buffer::Config stagingBufferConfig{};
    stagingBufferConfig.Device = m_Device;
    stagingBufferConfig.Size = textureAsset.Data.Size() * sizeof(uint8_t);
    stagingBufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
    stagingBufferConfig.CpuMapable = true;
    stagingBufferConfig.DebugName = "EnvMap Staging Buffer";

    VulkanHelper::Buffer stagingBuffer = VulkanHelper::Buffer::New(stagingBufferConfig).Value();

    VH_ASSERT(stagingBuffer.UploadData(pixels, textureAsset.Data.Size() * sizeof(uint8_t), 0) == VulkanHelper::VHResult::OK, "Failed to upload texture data");

    VH_ASSERT(stagingBuffer.CopyToImage(
        commandBuffer,
        textureImage
    ) == VulkanHelper::VHResult::OK, "Failed to copy staging buffer to image");

    // Finally send the alias map to the GPU
    VulkanHelper::Buffer::Config bufferConfig{};
    bufferConfig.Device = m_Device;
    bufferConfig.Size = sizeof(AliasMapEntry) * aliasMap.size();
    bufferConfig.Usage = VulkanHelper::Buffer::Usage::STORAGE_BUFFER_BIT | VulkanHelper::Buffer::Usage::TRANSFER_DST_BIT;
    bufferConfig.DebugName = "EnvAliasMap";

    m_EnvAliasMap = VulkanHelper::Buffer::New(bufferConfig).Value();

    bufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
    bufferConfig.CpuMapable = true;
    VulkanHelper::Buffer aliasMapStagingBuffer = VulkanHelper::Buffer::New(bufferConfig).Value();

    VH_ASSERT(aliasMapStagingBuffer.UploadData(aliasMap.data(), bufferConfig.Size, 0) == VulkanHelper::VHResult::OK, "Failed to upload environment alias map data");
    VH_ASSERT(m_EnvAliasMap.CopyFromBuffer(commandBuffer, aliasMapStagingBuffer, 0, 0, bufferConfig.Size) == VulkanHelper::VHResult::OK, "Failed to copy environment alias map buffer");
}

void PathTracer::AddVolume(const Volume& volume, VulkanHelper::CommandBuffer commandBuffer)
{
    const uint32_t initialSize = m_Volumes.size();

    VolumeGPU volumeGPU(volume);

    UploadDataToBuffer(m_VolumesBuffer, &volumeGPU, sizeof(VolumeGPU), m_Volumes.size() * sizeof(VolumeGPU), commandBuffer);
    m_Volumes.push_back(volume);

    // Update Uniform Buffer
    uint32_t count = (uint32_t)m_Volumes.size();
    UploadDataToBuffer(m_PathTracerUniformBuffer, &count, sizeof(uint32_t), offsetof(PathTracerUniform, VolumesCount), commandBuffer);
    ResetPathTracing();

    if (initialSize == 0)
        ReloadShaders(commandBuffer);
}

void PathTracer::AddDensityDataToVolume(uint32_t volumeIndex, const std::string& filepath, VulkanHelper::CommandBuffer commandBuffer)
{
    if (volumeIndex >= m_Volumes.size())
    {
        VH_LOG_ERROR("Volume index out of range: {}/{}", volumeIndex, m_Volumes.size());
        return;
    }
    auto& volume = m_Volumes[volumeIndex];

    // Check if the density data is already loaded for another volume, if so just reuse it, no point in wasting memory
    bool isDensityDataAlreadyLoaded = false;
    for (const auto& vol : m_Volumes)
    {
        if (vol.DensityDataFilepath == filepath)
        {
            volume.DensityTextureView = vol.DensityTextureView;
            volume.DensityDataIndex = vol.DensityDataIndex;
            volume.DensityDataFilepath = vol.DensityDataFilepath;
            volume.MaxDensitiesBuffer = vol.MaxDensitiesBuffer;
            isDensityDataAlreadyLoaded = true;
            break;
        }
    }

    if (std::filesystem::exists(filepath) == false)
    {
        VH_LOG_ERROR("OPENDVDB file does not exist: {}", filepath);
        return;
    }

    VH_LOG_DEBUG("Loading OPENDVDB volume: {}", filepath);
    openvdb::io::File file(filepath);

    try {
        file.open();  // This will throw if the file can't be opened
    } catch (const openvdb::IoError& e) {
        VH_LOG_ERROR("Failed to open OpenVDB file '{}': {}", filepath, e.what());
        return;
    }

    openvdb::GridBase::Ptr densityGrid;
    openvdb::GridBase::Ptr temperatureGrid;
    for (openvdb::io::File::NameIterator nameIter = file.beginName(); nameIter != file.endName(); ++nameIter)
    {
        VH_LOG_DEBUG("Found grid in VDB file: {}", nameIter.gridName());
        if (nameIter.gridName() == "density")
        {
            densityGrid = file.readGrid(nameIter.gridName());
        }

        if (nameIter.gridName() == "temperature" || nameIter.gridName() == "flames")
        {
            temperatureGrid = file.readGrid(nameIter.gridName());
        }
    }
    file.close();

    openvdb::FloatGrid::Ptr floatGridDensity = openvdb::gridPtrCast<openvdb::FloatGrid>(densityGrid);
    
    float maxDensity = openvdb::tools::minMax(floatGridDensity->tree(), true).max();
    
    float minTemperature = 0.0f;
    float maxTemperature = 0.0f;
    openvdb::FloatGrid::Ptr floatGridTemperature;
    if (temperatureGrid)
    {
        floatGridTemperature = openvdb::gridPtrCast<openvdb::FloatGrid>(temperatureGrid);
        minTemperature = openvdb::tools::minMax(floatGridTemperature->tree(), true).min();
        maxTemperature = openvdb::tools::minMax(floatGridTemperature->tree(), true).max();
        VH_LOG_DEBUG("Temperature range: {} - {}", minTemperature, maxTemperature);
    }

    openvdb::math::Coord dim = floatGridDensity->evalActiveVoxelDim();
    openvdb::math::Coord min = floatGridDensity->evalActiveVoxelBoundingBox().min();
    openvdb::math::Coord max = floatGridDensity->evalActiveVoxelBoundingBox().max();

    VH_LOG_DEBUG("Volume byte size: {} MB", ((float)dim.x() * (float)dim.y() * (float)dim.z() * sizeof(float)) / (1024.0f * 1024.0f));
    VH_LOG_DEBUG("Volume dimensions: {} x {} x {}", dim.x(), dim.y(), dim.z());
    VH_LOG_DEBUG("Max Density: {}", maxDensity);

    volume.CornerMin = glm::vec3(min.x(), min.y(), min.z());
    volume.CornerMax = glm::vec3(max.x(), max.y(), max.z());

    // Scale it down so AABB is more or less -1 to 1
    float maxDim = glm::max(glm::max(dim.x(), dim.y()), dim.z());
    volume.CornerMin /= maxDim / 2.0f;
    volume.CornerMax /= maxDim / 2.0f;

    if (!isDensityDataAlreadyLoaded)
    {
        volume.DensityDataFilepath = filepath;

        // Prepare density texture
        VulkanHelper::Image::Config imageConfig{};
        imageConfig.Device = m_Device;
        imageConfig.Width = (uint32_t)dim.x();
        imageConfig.Height = (uint32_t)dim.y();
        imageConfig.LayerCount = (uint32_t)dim.z();
        imageConfig.Format = VulkanHelper::Format::R32_SFLOAT;
        imageConfig.Usage = VulkanHelper::Image::Usage::SAMPLED_BIT | VulkanHelper::Image::Usage::TRANSFER_DST_BIT;
        imageConfig.UsePersistentStagingBuffer = true;

        VulkanHelper::Image densityImage = VulkanHelper::Image::New(imageConfig).Value();
        densityImage.TransitionImageLayout(VulkanHelper::Image::Layout::TRANSFER_DST_OPTIMAL, commandBuffer, 0, (uint32_t)dim.z());

        VulkanHelper::ImageView::Config imageViewConfig{};
        imageViewConfig.image = densityImage;
        imageViewConfig.ViewType = VulkanHelper::ImageView::ViewType::VIEW_2D_ARRAY;
        imageViewConfig.BaseLayer = 0;
        imageViewConfig.LayerCount = (uint32_t)dim.z();

        volume.DensityTextureView = VulkanHelper::ImageView::New(imageViewConfig).Value();

        // Prepare temperature texture if temperature grid is present
        VulkanHelper::Image temperatureImage;
        if (temperatureGrid)
        {
            temperatureImage = VulkanHelper::Image::New(imageConfig).Value();
            temperatureImage.TransitionImageLayout(VulkanHelper::Image::Layout::TRANSFER_DST_OPTIMAL, commandBuffer, 0, (uint32_t)dim.z());

            imageViewConfig.image = temperatureImage;
            volume.TemperatureTextureView = VulkanHelper::ImageView::New(imageViewConfig).Value();
        }

        // Prepare staging buffer
        VulkanHelper::Buffer::Config stagingBufferConfig{};
        stagingBufferConfig.Device = m_Device;
        stagingBufferConfig.Size = (uint32_t)dim.x() * (uint32_t)dim.y() * sizeof(float);
        stagingBufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
        stagingBufferConfig.CpuMapable = true;
        stagingBufferConfig.DebugName = "Volume Density Staging Buffer";

        VulkanHelper::Buffer stagingBuffer = VulkanHelper::Buffer::New(stagingBufferConfig).Value();

        // Prepare density data

        // For each volume there is 32x32x32 grid of max densities precomputed for empty space skipping
        std::array<float, 32768> volumeMaxDensities;
        volumeMaxDensities.fill(0.0f);

        std::vector<float> densityData;
        std::vector<float> temperatureData;
        densityData.reserve((size_t)dim.x() * (size_t)dim.y());
        temperatureData.reserve((size_t)dim.x() * (size_t)dim.y());
        for (int z = 0; z < dim.z(); z++)
        {
            densityData.clear();
            temperatureData.clear();
            for (int y = 0; y < dim.y(); y++)
            {
                for (int x = 0; x < dim.x(); x++)
                {
                    openvdb::math::Coord coord(min.x() + x, min.y() + (dim.y() - 1 - y), min.z() + z); // Y has to be flipped for vulkan

                    float density = floatGridDensity->tree().getValue(coord) / maxDensity; // Normalize to [0, 1]

                    int maxDensityGridIndex = ((x * 32) / dim.x()) + ((y * 32) / dim.y()) * 32 + ((z * 32) / dim.z()) * 1024;
                    if (volumeMaxDensities[(uint32_t)maxDensityGridIndex] < density)
                        volumeMaxDensities[(uint32_t)maxDensityGridIndex] = density;

                    densityData.push_back(density);

                    // If temperature grid is present, we can use it to modulate density to simulate fire-like volumes
                    if (temperatureGrid)
                    {
                        float temperature = floatGridTemperature->tree().getValue(coord);

                        // Store normalized temperature
                        temperature = glm::max((temperature - minTemperature) / (maxTemperature - minTemperature), 0.0f); // Normalize to [0, 1]

                        temperatureData.push_back(temperature);
                    }
                }
            }

            // Upload texture data one layer at a time

            // Density
            VH_ASSERT(stagingBuffer.UploadData(densityData.data(), densityData.size() * sizeof(float), 0) == VulkanHelper::VHResult::OK, "Failed to upload texture data");
            VH_ASSERT(stagingBuffer.CopyToImage(
                commandBuffer,
                densityImage,
                0,
                0,
                0,
                UINT32_MAX,
                UINT32_MAX,
                (uint32_t)z
            ) == VulkanHelper::VHResult::OK, "Failed to copy staging buffer to image");

            VH_ASSERT(commandBuffer.EndRecording() == VulkanHelper::VHResult::OK, "Failed to end command buffer recording");
            VH_ASSERT(commandBuffer.SubmitAndWait() == VulkanHelper::VHResult::OK, "Failed to submit and wait command buffer");
            VH_ASSERT(commandBuffer.BeginRecording(VulkanHelper::CommandBuffer::Usage::ONE_TIME_SUBMIT_BIT) == VulkanHelper::VHResult::OK, "Failed to begin command buffer recording");
        
            // Temperature
            if (temperatureGrid)
            {
                VH_ASSERT(stagingBuffer.UploadData(temperatureData.data(), temperatureData.size() * sizeof(float), 0) == VulkanHelper::VHResult::OK, "Failed to upload texture data");
                VH_ASSERT(stagingBuffer.CopyToImage(
                    commandBuffer,
                    temperatureImage,
                    0,
                    0,
                    0,
                    UINT32_MAX,
                    UINT32_MAX,
                    (uint32_t)z
                ) == VulkanHelper::VHResult::OK, "Failed to copy staging buffer to image");

                VH_ASSERT(commandBuffer.EndRecording() == VulkanHelper::VHResult::OK, "Failed to end command buffer recording");
                VH_ASSERT(commandBuffer.SubmitAndWait() == VulkanHelper::VHResult::OK, "Failed to submit and wait command buffer");
                VH_ASSERT(commandBuffer.BeginRecording(VulkanHelper::CommandBuffer::Usage::ONE_TIME_SUBMIT_BIT) == VulkanHelper::VHResult::OK, "Failed to begin command buffer recording");
            }
        }

        densityImage.TransitionImageLayout(VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL, commandBuffer, 0, (uint32_t)dim.z());
        if (temperatureGrid)
            temperatureImage.TransitionImageLayout(VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL, commandBuffer, 0, (uint32_t)dim.z());

        VulkanHelper::Buffer::Config bufferConfig{};
        bufferConfig.Device = m_Device;
        bufferConfig.Size = sizeof(float) * volumeMaxDensities.size();
        bufferConfig.Usage = VulkanHelper::Buffer::Usage::STORAGE_BUFFER_BIT | VulkanHelper::Buffer::Usage::TRANSFER_DST_BIT;
        bufferConfig.DebugName = "VolumeMaxDensities";

        volume.MaxDensitiesBuffer = VulkanHelper::Buffer::New(bufferConfig).Value();

        UploadDataToBuffer(volume.MaxDensitiesBuffer, volumeMaxDensities.data(), bufferConfig.Size, 0, commandBuffer);
        
        static int densityDataIndex = 0;
        VH_ASSERT(m_PathTracerDescriptorSet.AddImage(19, (uint32_t)densityDataIndex, &volume.DensityTextureView, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add volume density textures buffer to descriptor set");
        VH_ASSERT(m_PathTracerDescriptorSet.AddImage(20, (uint32_t)densityDataIndex, temperatureGrid ? &volume.TemperatureTextureView : nullptr, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add volume temperature textures buffer to descriptor set");
        VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(21, (uint32_t)densityDataIndex, &volume.MaxDensitiesBuffer) == VulkanHelper::VHResult::OK, "Failed to add volume max densities buffer to descriptor set");
        volume.DensityDataIndex = densityDataIndex;
        densityDataIndex = (densityDataIndex + 1) % (int)MAX_HETEROGENEOUS_VOLUMES;
    }

    SetVolume(volumeIndex, volume, commandBuffer);
}

void PathTracer::RemoveDensityDataFromVolume(uint32_t volumeIndex, VulkanHelper::CommandBuffer commandBuffer)
{
    auto& volume = m_Volumes[volumeIndex];
    volume.DensityDataFilepath = "";
    volume.DensityTextureView = VulkanHelper::ImageView();
    volume.DensityDataIndex = -1;
    volume.MaxDensitiesBuffer = VulkanHelper::Buffer();
    volume.CornerMin = glm::vec3(-1.0f);
    volume.CornerMax = glm::vec3(1.0f);
    SetVolume(volumeIndex, volume, commandBuffer);
}

void PathTracer::RemoveVolume(uint32_t index, VulkanHelper::CommandBuffer commandBuffer)
{
    const uint32_t initialSize = m_Volumes.size();

    // It will remove the volume from the array and move all subsequent volumes down to fill the gap
    uint32_t volumesToMove = (uint32_t)m_Volumes.size() - index - 1;
    m_Volumes.erase(m_Volumes.begin() + index);
    if (volumesToMove > 0)
    {
        std::vector<VolumeGPU> volumes(volumesToMove);
        DownloadDataFromBuffer(m_VolumesBuffer, volumes.data(), sizeof(VolumeGPU) * volumesToMove, (index + 1) * sizeof(VolumeGPU), commandBuffer);
        UploadDataToBuffer(m_VolumesBuffer, volumes.data(), sizeof(VolumeGPU) * volumesToMove, index * sizeof(VolumeGPU), commandBuffer);
    }

    uint32_t volumeCount = (uint32_t)m_Volumes.size();

    // Update Uniform Buffer
    UploadDataToBuffer(m_PathTracerUniformBuffer, &volumeCount, sizeof(uint32_t), offsetof(PathTracerUniform, VolumesCount), commandBuffer);
    ResetPathTracing();

    if (initialSize == 1)
        ReloadShaders(commandBuffer);
}

void PathTracer::SetVolume(uint32_t index, const Volume& volume, VulkanHelper::CommandBuffer commandBuffer)
{
    m_Volumes[index] = volume;
    VolumeGPU volumeGPU(volume);
    UploadDataToBuffer(m_VolumesBuffer, &volumeGPU, sizeof(VolumeGPU), index * sizeof(VolumeGPU), commandBuffer);
    ResetPathTracing();
}

VulkanHelper::ImageView PathTracer::LoadDefaultTexture(VulkanHelper::CommandBuffer commandBuffer, bool normal)
{
    std::vector<uint8_t> textureData(4);
    if (normal)
    {
        // Default normal map is (0.5, 0.5, 1.0) in RGB
        textureData[0] = 128;
        textureData[1] = 128;
        textureData[2] = 255;
        textureData[3] = 255;
    }
    else
    {
        // Default white texture is (1.0, 1.0, 1.0) in RGBA
        textureData[0] = 255;
        textureData[1] = 255;
        textureData[2] = 255;
        textureData[3] = 255;
    }

    VulkanHelper::Image::Config imageConfig{};
    imageConfig.Device = m_Device;
    imageConfig.Width = 1;
    imageConfig.Height = 1;
    imageConfig.Format = VulkanHelper::Format::R8G8B8A8_UNORM;
    imageConfig.Usage = VulkanHelper::Image::Usage::SAMPLED_BIT | VulkanHelper::Image::Usage::TRANSFER_DST_BIT;

    VulkanHelper::Image textureImage = VulkanHelper::Image::New(imageConfig).Value();

    // Create staging buffer
    VulkanHelper::Buffer::Config stagingBufferConfig{};
    stagingBufferConfig.Device = m_Device;
    stagingBufferConfig.Size = textureData.size() * sizeof(uint8_t);
    stagingBufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
    stagingBufferConfig.CpuMapable = true;
    stagingBufferConfig.DebugName = "Default Texture Staging Buffer";

    VulkanHelper::Buffer stagingBuffer = VulkanHelper::Buffer::New(stagingBufferConfig).Value();

    VH_ASSERT(stagingBuffer.UploadData(textureData.data(), textureData.size() * sizeof(uint8_t), 0) == VulkanHelper::VHResult::OK, "Failed to upload texture data");

    textureImage.TransitionImageLayout(VulkanHelper::Image::Layout::TRANSFER_DST_OPTIMAL, commandBuffer);

    VH_ASSERT(stagingBuffer.CopyToImage(
        commandBuffer,
        textureImage
    ) == VulkanHelper::VHResult::OK, "Failed to copy staging buffer to image");

    textureImage.TransitionImageLayout(VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL, commandBuffer);

    VulkanHelper::ImageView::Config imageViewConfig{};
    imageViewConfig.image = textureImage;
    imageViewConfig.ViewType = VulkanHelper::ImageView::ViewType::VIEW_2D;
    imageViewConfig.BaseLayer = 0;
    imageViewConfig.LayerCount = 1;

    return VulkanHelper::ImageView::New(imageViewConfig).Value();
}

void PathTracer::SetEnvMapMIS(bool value, VulkanHelper::CommandBuffer commandBuffer)
{
    m_EnableEnvMapMIS = value;
    ResetPathTracing();
    ReloadShaders(commandBuffer);
}

void PathTracer::SetEnvMapShownDirectly(bool value, VulkanHelper::CommandBuffer commandBuffer)
{
    m_ShowEnvMapDirectly = value;
    ResetPathTracing();
    ReloadShaders(commandBuffer);
}

void PathTracer::SetUseOnlyGeometryNormals(bool useOnlyGeometryNormals, VulkanHelper::CommandBuffer commandBuffer)
{
    m_UseOnlyGeometryNormals = useOnlyGeometryNormals;
    ResetPathTracing();
    ReloadShaders(commandBuffer);
}

void PathTracer::SetUseEnergyCompensation(bool useEnergyCompensation, VulkanHelper::CommandBuffer commandBuffer)
{
    m_UseEnergyCompensation = useEnergyCompensation;
    ResetPathTracing();
    ReloadShaders(commandBuffer);
}

void PathTracer::SetFurnaceTestMode(bool furnaceTestMode, VulkanHelper::CommandBuffer commandBuffer)
{
    m_FurnaceTestMode = furnaceTestMode;
    ResetPathTracing();
    ReloadShaders(commandBuffer);
}

void PathTracer::SetEnvironmentIntensity(float environmentIntensity, VulkanHelper::CommandBuffer commandBuffer)
{
    m_EnvironmentIntensity = environmentIntensity;
    UploadDataToBuffer(m_PathTracerUniformBuffer, &m_EnvironmentIntensity, sizeof(float), offsetof(PathTracerUniform, EnvironmentIntensity), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetUseRayQueries(bool useRayQueries, VulkanHelper::CommandBuffer commandBuffer)
{
    m_UseRayQueries = useRayQueries;
    ResetPathTracing();
    ReloadShaders(commandBuffer);
}

void PathTracer::SetCameraViewInverse(const glm::mat4& view, VulkanHelper::CommandBuffer commandBuffer)
{
    m_CameraViewInverse = view;

    UploadDataToBuffer(m_PathTracerUniformBuffer, &m_CameraViewInverse, sizeof(glm::mat4), offsetof(PathTracerUniform, CameraViewInverse), commandBuffer);

    ResetPathTracing();
}

void PathTracer::SetCameraProjectionInverse(const glm::mat4& projection, VulkanHelper::CommandBuffer commandBuffer)
{
    m_CameraProjectionInverse = projection;

    UploadDataToBuffer(m_PathTracerUniformBuffer, &m_CameraProjectionInverse, sizeof(glm::mat4), offsetof(PathTracerUniform, CameraProjectionInverse), commandBuffer);

    ResetPathTracing();
}

void PathTracer::SetPhaseFunction(PhaseFunction phaseFunction, VulkanHelper::CommandBuffer commandBuffer)
{
    m_PhaseFunction = phaseFunction;
    ResetPathTracing();
    ReloadShaders(commandBuffer);
}

void PathTracer::SetSplitScreenCount(uint32_t count, VulkanHelper::CommandBuffer commandBuffer)
{
    m_ScreenChunkCount = count;
    UploadDataToBuffer(m_PathTracerUniformBuffer, &count, sizeof(uint32_t), offsetof(PathTracerUniform, ScreenChunkCount), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetEnableAtmosphere(bool enabled, VulkanHelper::CommandBuffer commandBuffer)
{
    m_EnableAtmosphere = enabled;
    ResetPathTracing();
    ReloadShaders(commandBuffer);
}