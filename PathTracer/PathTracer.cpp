#include "PathTracer.h"

#include <filesystem>
#include <chrono>
#include <array>
#include <fstream>
#include <numeric>

PathTracer PathTracer::New(const VulkanHelper::Device& device, VulkanHelper::ThreadPool* threadPool)
{
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
    uniformBufferConfig.CpuMapable = true;
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
    volumesBufferConfig.Size = sizeof(Volume) * MAX_ENTITIES;
    volumesBufferConfig.Usage = VulkanHelper::Buffer::Usage::STORAGE_BUFFER_BIT | VulkanHelper::Buffer::Usage::TRANSFER_DST_BIT | VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
    pathTracer.m_VolumesBuffer = VulkanHelper::Buffer::New(volumesBufferConfig).Value();

    return pathTracer;
}

bool PathTracer::PathTrace(VulkanHelper::CommandBuffer& commandBuffer)
{
    if (m_SamplesAccumulated >= m_MaxSamplesAccumulated)
        return true;

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

    Data data;
    data.FrameCount = m_FrameCount;
    data.Seed = PCGHash(timeElapsed); // Random seed for each frame
    
    VH_ASSERT(m_PathTracerUniformBuffer.UploadData(&data, sizeof(Data), offsetof(PathTracerUniform, FrameCount), &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload path tracer uniform buffer");
    
    m_PathTracerPipeline.Bind(commandBuffer);
    m_PathTracerPipeline.RayTrace(commandBuffer, m_OutputImageView.GetImage().GetWidth(), m_OutputImageView.GetImage().GetHeight());
    m_FrameCount++;
    m_SamplesAccumulated += m_SamplesPerFrame;

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

    VH_ASSERT(scene.Value().Cameras.Size() > 0, "No cameras found in scene! Please load a scene that contains a camera!");
    VH_ASSERT(scene.Value().Meshes.Size() > 0, "No meshes found in scene! Please load a scene that contains meshes!");

    // Load Camera values
    const float aspectRatio = scene.Value().Cameras[0].AspectRatio;
    m_FOV = scene.Value().Cameras[0].FOV;
    glm::mat4 cameraView = scene.Value().Cameras[0].ViewMatrix;

    std::array<VulkanHelper::Format, 3> vertexAttributes = {
        VulkanHelper::Format::R32G32B32_SFLOAT, // Position
        VulkanHelper::Format::R32G32B32_SFLOAT, // Normal
        VulkanHelper::Format::R32G32_SFLOAT, // UV
    };

    VulkanHelper::CommandBuffer initializationCmd = m_CommandPool.AllocateCommandBuffer({VulkanHelper::CommandBuffer::Level::PRIMARY}).Value();
    VH_ASSERT(initializationCmd.BeginRecording(VulkanHelper::CommandBuffer::Usage::ONE_TIME_SUBMIT_BIT) == VulkanHelper::VHResult::OK, "Failed to begin recording initialization command buffer");

    m_ReflectionLookup = LoadLookupTable("../../../Assets/LookupTables/ReflectionLookup.bin", {64, 64, 32}, initializationCmd);
    m_RefractionFromOutsideLookup = LoadLookupTable("../../../Assets/LookupTables/RefractionLookupHitFromOutside.bin", {128, 128, 32}, initializationCmd);
    m_RefractionFromInsideLookup = LoadLookupTable("../../../Assets/LookupTables/RefractionLookupHitFromInside.bin", {128, 128, 32}, initializationCmd);
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
    VH_ASSERT(m_MaterialsBuffer.UploadData(
        m_Materials.data(),
        m_Materials.size() * sizeof(Material),
        0,
        &initializationCmd
    ) == VulkanHelper::VHResult::OK, "Failed to upload materials buffer");

    VH_ASSERT(initializationCmd.EndRecording() == VulkanHelper::VHResult::OK, "Failed to end recording initialization command buffer");
    VH_ASSERT(initializationCmd.SubmitAndWait() == VulkanHelper::VHResult::OK, "Failed to submit initialization command buffer");
    VH_ASSERT(initializationCmd.BeginRecording(VulkanHelper::CommandBuffer::Usage::ONE_TIME_SUBMIT_BIT) == VulkanHelper::VHResult::OK, "Failed to begin recording initialization command buffer");

    VulkanHelper::Vector<VulkanHelper::BLAS> blasVector;
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
    std::array<VulkanHelper::DescriptorSet::BindingDescription, 19> bindingDescriptions = {
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
        VulkanHelper::DescriptorSet::BindingDescription{18, 1, VulkanHelper::ShaderStages::CLOSEST_HIT_BIT, VulkanHelper::DescriptorType::SAMPLER} // Lookup sampler
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
    for (uint32_t i = 0; i < m_SceneBaseColorTextures.size(); ++i)
        VH_ASSERT(m_PathTracerDescriptorSet.AddImage(5, i, m_SceneBaseColorTextures[i], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add albedo texture to descriptor set");
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
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(12, 0, m_ReflectionLookup, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add reflection lookup texture to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(13, 0, m_RefractionFromOutsideLookup, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add refraction hit from outside lookup texture to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(14, 0, m_RefractionFromInsideLookup, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add reflection hit from inside lookup texture to descriptor set");

    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(15, 0, m_EnvMapTexture, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add env map texture to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(16, 0, m_EnvAliasMap) == VulkanHelper::VHResult::OK, "Failed to add env alias map buffer to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(17, 0, m_VolumesBuffer) == VulkanHelper::VHResult::OK, "Failed to add volumes buffer to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddSampler(18, 0, m_LookupTableSampler) == VulkanHelper::VHResult::OK, "Failed to add lookup table sampler to descriptor set");

    // Upload Path Tracer uniform data
    PathTracerUniform pathTracerUniform{};
    glm::mat4 cameraProjection = glm::perspective(glm::radians(m_FOV), aspectRatio, 0.1f, 100.0f);
    pathTracerUniform.CameraViewInverse = glm::inverse(cameraView);
    pathTracerUniform.CameraProjectionInverse = glm::inverse(cameraProjection);
    pathTracerUniform.MaxDepth = m_MaxDepth;
    pathTracerUniform.SampleCount = m_SamplesPerFrame;
    pathTracerUniform.MaxLuminance = m_MaxLuminance;
    pathTracerUniform.FocusDistance = m_FocusDistance;
    pathTracerUniform.DepthOfFieldStrength = m_DepthOfFieldStrength;
    pathTracerUniform.EnvMapRotationAzimuth = m_EnvMapRotationAzimuth;
    pathTracerUniform.EnvMapRotationAltitude = m_EnvMapRotationAltitude;
    pathTracerUniform.VolumesCount = 0; // Starts empty

    VH_ASSERT(m_PathTracerUniformBuffer.UploadData(&pathTracerUniform, sizeof(PathTracerUniform), 0, &initializationCmd) == VulkanHelper::VHResult::OK, "Failed to upload path tracer uniform data");

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

    VulkanHelper::Shader::InitializeSession("../../../PathTracer/Shaders/", defines.size(), defines.data());
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
    pipelineConfig.CommandBuffer = &initializationCmd;

    m_PathTracerPipeline = VulkanHelper::Pipeline::New(pipelineConfig).Value();

    VH_ASSERT(initializationCmd.EndRecording() == VulkanHelper::VHResult::OK, "Failed to end recording initialization command buffer");
    VH_ASSERT(initializationCmd.SubmitAndWait() == VulkanHelper::VHResult::OK, "Failed to submit initialization command buffer");
}

void PathTracer::ResizeImage(uint32_t width, uint32_t height, VulkanHelper::CommandBuffer commandBuffer)
{
    m_Width = width;
    m_Height = height;

    // Update the output image view with the new dimensions
    CreateOutputImageView();

    // Update descriptor set
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(0, 0, m_OutputImageView, VulkanHelper::Image::Layout::GENERAL) == VulkanHelper::VHResult::OK, "Failed to add output image view to descriptor set");

    // Update projection
    const float aspectRatio = static_cast<float>(m_Width) / static_cast<float>(m_Height);
    glm::mat4 cameraProjection = glm::perspective(glm::radians(m_FOV), aspectRatio, 0.1f, 100.0f);

    cameraProjection = glm::inverse(cameraProjection);

    VH_ASSERT(m_PathTracerUniformBuffer.UploadData(&cameraProjection, sizeof(glm::mat4), offsetof(PathTracerUniform, CameraProjectionInverse), &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload path tracer uniform data");

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

    // Update material buffer
    VH_ASSERT(m_MaterialsBuffer.UploadData(&material, sizeof(Material), index * sizeof(Material), &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload material data");
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

    // Upload texture data
    VH_ASSERT(textureImage.UploadData(
        textureAsset.Data.Data(),
        textureAsset.Data.Size() * sizeof(uint8_t),
        0,
        &commandBuffer
    ) == VulkanHelper::VHResult::OK, "Failed to upload texture data");

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

    std::ifstream file(filepath, std::ios::binary);
    VH_ASSERT(file, "Failed to open reflection lookup table");

    // Read the file contents into a buffer
    std::vector<uint8_t> buffer(tableSize.x * tableSize.y * tableSize.z * sizeof(float));
    file.read(reinterpret_cast<char*>(buffer.data()), (std::streamsize)buffer.size());
    file.close();

    // Upload texture data one layer at a time
    for (uint64_t i = 0; i < tableSize.z; i++)
    {
        VH_ASSERT(textureImage.UploadData(
            buffer.data() + (i * (uint64_t)tableSize.y * (uint64_t)tableSize.x * (uint64_t)sizeof(float)),
            tableSize.y * tableSize.x * sizeof(float),
            0,
            &commandBuffer,
            i
        ) == VulkanHelper::VHResult::OK, "Failed to upload texture data");
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
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(5, index, m_SceneBaseColorTextures[index], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add base color texture to descriptor set");
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
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(6, index, m_SceneNormalTextures[index], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add normal texture to descriptor set");
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
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(7, index, m_SceneRoughnessTextures[index], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add roughness texture to descriptor set");
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
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(8, index, m_SceneMetallicTextures[index], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add metallic texture to descriptor set");
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
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(9, index, m_SceneEmissiveTextures[index], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add emissive texture to descriptor set");
    ResetPathTracing();
}

void PathTracer::SetMaxDepth(uint32_t maxDepth, VulkanHelper::CommandBuffer commandBuffer)
{
    if (m_MaxDepth == maxDepth)
        return;
    
    m_MaxDepth = maxDepth;
    VH_ASSERT(m_PathTracerUniformBuffer.UploadData(&maxDepth, sizeof(uint32_t), offsetof(PathTracerUniform, MaxDepth), &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload path tracer uniform data");
    ResetPathTracing();
}

void PathTracer::SetMaxSamplesAccumulated(uint32_t maxSamples)
{
    m_MaxSamplesAccumulated = maxSamples;
}

void PathTracer::SetSamplesPerFrame(uint32_t samplesPerFrame, VulkanHelper::CommandBuffer commandBuffer)
{
    if (m_SamplesPerFrame == samplesPerFrame)
        return;

    m_SamplesPerFrame = samplesPerFrame;
    VH_ASSERT(m_PathTracerUniformBuffer.UploadData(&samplesPerFrame, sizeof(uint32_t), offsetof(PathTracerUniform, SampleCount), &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload path tracer uniform data");
    ResetPathTracing();
}

void PathTracer::SetMaxLuminance(float maxLuminance, VulkanHelper::CommandBuffer commandBuffer)
{
    if (m_MaxLuminance == maxLuminance)
        return;

    m_MaxLuminance = maxLuminance;
    VH_ASSERT(m_PathTracerUniformBuffer.UploadData(&maxLuminance, sizeof(float), offsetof(PathTracerUniform, MaxLuminance), &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload path tracer uniform data");
    ResetPathTracing();
}

void PathTracer::SetFocusDistance(float focusDistance, VulkanHelper::CommandBuffer commandBuffer)
{
    if (m_FocusDistance == focusDistance)
        return;

    m_FocusDistance = focusDistance;
    VH_ASSERT(m_PathTracerUniformBuffer.UploadData(&focusDistance, sizeof(float), offsetof(PathTracerUniform, FocusDistance), &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload path tracer uniform data");
    ResetPathTracing();
}

void PathTracer::SetDepthOfFieldStrength(float depthOfFieldStrength, VulkanHelper::CommandBuffer commandBuffer)
{
    if (m_DepthOfFieldStrength == depthOfFieldStrength)
        return;

    m_DepthOfFieldStrength = depthOfFieldStrength;
    VH_ASSERT(m_PathTracerUniformBuffer.UploadData(&depthOfFieldStrength, sizeof(float), offsetof(PathTracerUniform, DepthOfFieldStrength), &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload path tracer uniform data");
    ResetPathTracing();
}

void PathTracer::SetEnvMapFilepath(const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer)
{
    if (m_EnvMapFilepath == filePath)
        return;
    
    m_EnvMapFilepath = filePath;

    LoadEnvironmentMap(filePath, commandBuffer);
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(15, 0, m_EnvMapTexture, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add env map texture to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(16, 0, m_EnvAliasMap) == VulkanHelper::VHResult::OK, "Failed to add env alias map buffer to descriptor set");
    ResetPathTracing();
}

void PathTracer::SetEnvMapAzimuth(float azimuth, VulkanHelper::CommandBuffer commandBuffer)
{
    if (m_EnvMapRotationAzimuth == azimuth)
        return;

    m_EnvMapRotationAzimuth = azimuth;
    VH_ASSERT(m_PathTracerUniformBuffer.UploadData(&azimuth, sizeof(float), offsetof(PathTracerUniform, EnvMapRotationAzimuth), &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload path tracer uniform data");
    ResetPathTracing();
}

void PathTracer::SetEnvMapAltitude(float altitude, VulkanHelper::CommandBuffer commandBuffer)
{
    if (m_EnvMapRotationAltitude == altitude)
        return;

    m_EnvMapRotationAltitude = altitude;
    VH_ASSERT(m_PathTracerUniformBuffer.UploadData(&altitude, sizeof(float), offsetof(PathTracerUniform, EnvMapRotationAltitude), &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload path tracer uniform data");
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

    VulkanHelper::Shader::InitializeSession("../../../PathTracer/Shaders/", defines.size(), defines.data());
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

    // Reference: [https://github.com/nvpro-samples/nvpro_core/blob/master/nvvkhl/hdr_env.cpp]
    // Create Importance Buffer for Importance Sampling
    struct AliasMapEntry
    {
        uint32_t Alias; // Alias pointing to another texel
        float Importance; // Importance of the current texel
    };

    std::vector<AliasMapEntry> importanceBuffer(width * height);
    std::vector<float> importanceData(width * height);

    float cosTheta0 = 1.0F; // cosine of the up vector
	const float stepPhi = (float)2.0F * (float)M_PI / (float)width; // azimuth step
	const float stepTheta = (float)M_PI / (float)height; // altitude step

    // For each texel of the environment map, we compute the related solid angle
	// subtended by the texel, and store the weighted luminance in importanceData,
	// representing the amount of energy emitted through each texel.
    for (uint32_t y = 0; y < height; ++y)
	{
		const float theta1 = (float)(y + 1) * stepTheta; // altitude angle of currently sampled texel
		const float cosTheta1 = glm::cos(theta1); // cosine of the altitude angle

		// Calculate how much area does each texel take
		// (cosTheta0 - cosTheta1) - how much of the unit sphere does texel take
		//  * stepPhi - get solid angle

		const float area = (cosTheta0 - cosTheta1) * stepPhi;  // solid angle
		cosTheta0 = cosTheta1; // set cosine of the up vector to the altitude cosine to advance the loop

		for (uint32_t x = 0; x < width; ++x)
		{
			const uint32_t idx = y * width + x;
			const uint32_t idx4 = idx * 4; // texel index
            
			// Store the radiance of the texel into importance array, importance will be higher for brighter texels
			importanceData[idx] = area * glm::max(pixels[idx4], glm::max(pixels[idx4 + 1], pixels[idx4 + 2]));
		}
	}

    // Creating Alias Map
    //
	// Alias map is used to efficiently select texels from env map based on importance.
	// It aims at creating a set of texel couples
	// so that all couples emit roughly the same amount of energy. To do this,
	// each smaller radiance texel will be assigned an "alias" with higher emitted radiance
    std::vector<AliasMapEntry> aliasMap(size);

	// Compute the integral of the emitted radiance of the environment map
	// Since each element in data is already weighted by its solid angle it's just a sum
    float sum = std::accumulate(importanceData.begin(), importanceData.end(), 0.0f);
    float average = sum / float(size);
	for (uint32_t i = 0; i < size; i++)
	{
		// Calculate PDF. Inside PDF average of all values must be equal to 1, that's
		// why we divide texel importance from data by the average of all texels
		aliasMap[i].Importance = importanceData[i] / average;

		// identity, ie. each texel is its own alias
		aliasMap[i].Alias = i;
	}

    // Partition the texels according to their importance.
	// Texels with a value q < 1 (ie. below average) are stored incrementally from the beginning of the
	// array, while texels emitting higher-than-average radiance are stored from the end of the array.
	// This effectively separates the texels into two groups: one containing texels with below-average 
	// radiance and the other containing texels with above-average radiance
	std::vector<uint32_t> partitionTable(size);
	uint32_t              lowEnergyCounter = 0U;
	uint32_t              HighEnergyCounter = size;
	for (uint32_t i = 0; i < size; ++i)
	{
		if (aliasMap[i].Importance < 1.F)
		{
			lowEnergyCounter++;
			partitionTable[lowEnergyCounter] = i;
		}
		else
		{
			HighEnergyCounter--;
			partitionTable[HighEnergyCounter] = i;
		}
	}

    // Associate the lower-energy texels to higher-energy ones.
	for (lowEnergyCounter = 0; lowEnergyCounter < HighEnergyCounter && HighEnergyCounter < size; lowEnergyCounter++)
	{
		const uint32_t smallEnergyIndex = partitionTable[lowEnergyCounter];
		const uint32_t highEnergyIndex = partitionTable[HighEnergyCounter];

		// Associate the texel to its higher-energy alias
		aliasMap[smallEnergyIndex].Alias = highEnergyIndex;

		// Compute the difference between the lower-energy texel and the average
		const float differenceWithAverage = 1.F - aliasMap[smallEnergyIndex].Importance;

		// The goal is to obtain texel couples whose combined intensity is close to the average.
		// However, some texels may have low intensity, while others may have very high intensity. In this case
		// it may not be possible to obtain a value close to average by combining only two texels.
		// Instead, we potentially associate a single high-energy texel to many smaller-energy ones until
		// the combined average is similar to the average of the environment map.
		// We keep track of the combined average by subtracting the difference between the lower-energy texel and the average
        // from the ratio stored in the high-energy texel.
		aliasMap[highEnergyIndex].Importance -= differenceWithAverage;

		// If the combined ratio to average of the higher-energy texel reaches 1, a balance has been found
		// between a set of low-energy texels and the higher-energy one. In this case, we will use the next
		// higher-energy texel in the partition when processing the next texel.
		if (aliasMap[highEnergyIndex].Importance < 1.0f)
		{
			HighEnergyCounter++;
		}
	}

    // The PDF of each texel is computed by normalizing its emitted radiance by the sum of all emitted radiance
	for (uint32_t i = 0; i < width * height; ++i)
	{
		const uint32_t idx4 = i * 4;
		// Store the PDF inside Alpha channel(idx4 + 3)
		pixels[idx4 + 3] = glm::max(pixels[idx4], glm::max(pixels[idx4 + 1], pixels[idx4 + 2])) / sum;
	}

    // Upload texture data
    VH_ASSERT(textureImage.UploadData(
        pixels,
        textureAsset.Data.Size() * sizeof(uint8_t),
        0,
        &commandBuffer
    ) == VulkanHelper::VHResult::OK, "Failed to upload texture data");

    // Finally send the alias map to the GPU
    VulkanHelper::Buffer::Config bufferConfig{};
    bufferConfig.Device = m_Device;
    bufferConfig.Size = sizeof(AliasMapEntry) * aliasMap.size();
    bufferConfig.Usage = VulkanHelper::Buffer::Usage::STORAGE_BUFFER_BIT | VulkanHelper::Buffer::Usage::TRANSFER_DST_BIT;
    bufferConfig.DebugName = "EnvAliasMap";

    m_EnvAliasMap = VulkanHelper::Buffer::New(bufferConfig).Value();

    VH_ASSERT(m_EnvAliasMap.UploadData(aliasMap.data(), bufferConfig.Size, 0, &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload environment alias map data");
}

void PathTracer::AddVolume(const Volume& volume, VulkanHelper::CommandBuffer commandBuffer)
{
    const uint32_t initialSize = m_Volumes.size();
    VH_ASSERT(m_VolumesBuffer.UploadData(&volume, sizeof(Volume), m_Volumes.size() * sizeof(Volume), &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload volume data");
    m_Volumes.push_back(volume);

    // Update Uniform Buffer
    uint32_t count = (uint32_t)m_Volumes.size();
    VH_ASSERT(m_PathTracerUniformBuffer.UploadData(&count, sizeof(uint32_t), offsetof(PathTracerUniform, VolumesCount), &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload volume count");
    ResetPathTracing();

    if (initialSize == 0)
        ReloadShaders(commandBuffer);
}

void PathTracer::RemoveVolume(uint32_t index, VulkanHelper::CommandBuffer commandBuffer)
{
    const uint32_t initialSize = m_Volumes.size();

    // It will remove the volume from the array and move all subsequent volumes down to fill the gap
    uint32_t volumesToMove = (uint32_t)m_Volumes.size() - index - 1;
    m_Volumes.erase(m_Volumes.begin() + index);
    if (volumesToMove > 0)
    {
        Volume volume;
        VH_ASSERT(m_VolumesBuffer.DownloadData(&volume, sizeof(Volume) * volumesToMove, (index + 1) * sizeof(Volume), &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to download volume data");
        VH_ASSERT(m_VolumesBuffer.UploadData(&volume, sizeof(Volume) * volumesToMove, index * sizeof(Volume), &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload volume data");
    }

    uint32_t volumeCount = (uint32_t)m_Volumes.size();

    // Update Uniform Buffer
    VH_ASSERT(m_PathTracerUniformBuffer.UploadData(&volumeCount, sizeof(uint32_t), offsetof(PathTracerUniform, VolumesCount), &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload volume count");
    ResetPathTracing();

    if (initialSize == 1)
        ReloadShaders(commandBuffer);
}

void PathTracer::SetVolume(uint32_t index, const Volume& volume, VulkanHelper::CommandBuffer commandBuffer)
{
    m_Volumes[index] = volume;
    VH_ASSERT(m_VolumesBuffer.UploadData(&volume, sizeof(Volume), index * sizeof(Volume), &commandBuffer) == VulkanHelper::VHResult::OK, "Failed to upload volume data");
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

    // Upload texture data
    VH_ASSERT(textureImage.UploadData(
        textureData.data(),
        textureData.size() * sizeof(uint8_t),
        0,
        &commandBuffer
    ) == VulkanHelper::VHResult::OK, "Failed to upload texture data");

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
    if (m_EnableEnvMapMIS == value)
        return;
    
    m_EnableEnvMapMIS = value;
    ResetPathTracing();
    ReloadShaders(commandBuffer);
}

void PathTracer::SetEnvMapShownDirectly(bool value, VulkanHelper::CommandBuffer commandBuffer)
{
    if (m_ShowEnvMapDirectly == value)
        return;

    m_ShowEnvMapDirectly = value;
    ResetPathTracing();
    ReloadShaders(commandBuffer);
}

void PathTracer::SetUseOnlyGeometryNormals(bool useOnlyGeometryNormals, VulkanHelper::CommandBuffer commandBuffer)
{
    if (m_UseOnlyGeometryNormals == useOnlyGeometryNormals)
        return;

    m_UseOnlyGeometryNormals = useOnlyGeometryNormals;
    ResetPathTracing();
    ReloadShaders(commandBuffer);
}

void PathTracer::SetUseEnergyCompensation(bool useEnergyCompensation, VulkanHelper::CommandBuffer commandBuffer)
{
    if (m_UseEnergyCompensation == useEnergyCompensation)
        return;

    m_UseEnergyCompensation = useEnergyCompensation;
    ResetPathTracing();
    ReloadShaders(commandBuffer);
}