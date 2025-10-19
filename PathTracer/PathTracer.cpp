#include "PathTracer.h"

#include <cstdint>
#include <filesystem>
#include <chrono>
#include <array>
#include <fstream>
#include <glm/ext/matrix_transform.hpp>
#include <numeric>
#include <numbers>

#include "Log/Log.h"
#include "Vulkan/BLASBuilder.h"
#include "Vulkan/Buffer.h"
#include "Vulkan/CommandBuffer.h"

#define NANOVDB_USE_OPENVDB
#include "openvdb/openvdb.h"
#include "nanovdb/tools/CreateNanoGrid.h"

PathTracer PathTracer::New(const VulkanHelper::Device& device, VulkanHelper::ThreadPool* threadPool)
{
    openvdb::initialize();

    PathTracer pathTracer{};
    pathTracer.m_Device = device;
    pathTracer.m_ThreadPool = threadPool;

    pathTracer.m_CommandPoolGraphics = VulkanHelper::CommandPool::New({
        .Device = device,
        .QueueFamilyIndex = device.GetQueueFamilyIndices().GraphicsFamily
    }).Value();

    pathTracer.m_CommandPoolCompute = VulkanHelper::CommandPool::New({
        .Device = device,
        .QueueFamilyIndex = device.GetQueueFamilyIndices().ComputeFamily
    }).Value();

    // Descriptor pool

    // Just add 100k of each type for now. TODO, switch the pool when it runs out
    std::array<VulkanHelper::DescriptorPool::PoolSize, 7> poolSizes = {
        VulkanHelper::DescriptorPool::PoolSize{VulkanHelper::DescriptorType::SAMPLER, 100000},
        VulkanHelper::DescriptorPool::PoolSize{VulkanHelper::DescriptorType::COMBINED_IMAGE_SAMPLER, 100000},
        VulkanHelper::DescriptorPool::PoolSize{VulkanHelper::DescriptorType::SAMPLED_IMAGE, 100000},
        VulkanHelper::DescriptorPool::PoolSize{VulkanHelper::DescriptorType::STORAGE_IMAGE, 100000},
        VulkanHelper::DescriptorPool::PoolSize{VulkanHelper::DescriptorType::UNIFORM_BUFFER, 100000},
        VulkanHelper::DescriptorPool::PoolSize{VulkanHelper::DescriptorType::STORAGE_BUFFER, 100000},
        VulkanHelper::DescriptorPool::PoolSize{VulkanHelper::DescriptorType::ACCELERATION_STRUCTURE_KHR, 10000}
    };

    VulkanHelper::DescriptorPool::Config descriptorPoolConfig{};
    descriptorPoolConfig.Device = device;
    descriptorPoolConfig.MaxSets = 10000;
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

    VulkanHelper::Buffer::Config materialIndicesBufferConfig{};
    materialIndicesBufferConfig.Device = device;
    materialIndicesBufferConfig.Size = sizeof(uint32_t) * 2 * MAX_INSTANCES; // Store both material and mesh index for each instance
    materialIndicesBufferConfig.Usage = VulkanHelper::Buffer::Usage::STORAGE_BUFFER_BIT | VulkanHelper::Buffer::Usage::TRANSFER_DST_BIT;
    pathTracer.m_MaterialAndMeshIndicesBuffer = VulkanHelper::Buffer::New(materialIndicesBufferConfig).Value();

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
    m_PathTracerPipeline.RayTrace(
        commandBuffer,
        (uint32_t)glm::ceil((float)m_OutputImageView.GetImage().GetWidth() / (float)m_ScreenChunkCount),
        (uint32_t)glm::ceil((float)m_OutputImageView.GetImage().GetHeight() / (float)m_ScreenChunkCount)
    );
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

    VH_ASSERT(scene.Value().Meshes.Size() < MAX_ENTITIES, "Too many meshes in the scene: {}! The maximum supported number of meshes is {}", scene.Value().Meshes.Size(), MAX_ENTITIES);
    VH_ASSERT(scene.Value().Materials.Size() < MAX_ENTITIES, "Too many materials in the scene: {}! The maximum supported number of materials is {}", scene.Value().Materials.Size(), MAX_ENTITIES);
    VH_ASSERT(scene.Value().MeshInstances.Size() < MAX_INSTANCES, "Too many mesh instances in the scene: {}! The maximum supported number of mesh instances is {}", scene.Value().MeshInstances.Size(), MAX_INSTANCES);

    // Apply transforms to meshes
    for (auto& instance : scene.Value().MeshInstances)
    {
        if (instance.MeshIndex < scene.Value().Meshes.Size())
        {
            for (auto& vertex : scene.Value().Meshes[instance.MeshIndex].Vertices)
            {
                vertex.Position = glm::vec3(instance.Transform * glm::vec4(vertex.Position, 1.0f));
                vertex.Normal = glm::mat3(glm::transpose(glm::inverse(instance.Transform))) * vertex.Normal;
            }
        }
    }

    // Load Camera values
    const float aspectRatio = scene.Value().Cameras[0].AspectRatio;
    m_CameraViewInverse = glm::inverse(scene.Value().Cameras[0].ViewMatrix);

    std::array<VulkanHelper::Format, 3> vertexAttributes = {
        VulkanHelper::Format::R32G32B32_SFLOAT, // Position
        VulkanHelper::Format::R32G32B32_SFLOAT, // Normal
        VulkanHelper::Format::R32G32_SFLOAT, // UV
    };

    VulkanHelper::CommandBuffer initializationCmd = m_CommandPoolGraphics.AllocateCommandBuffer({VulkanHelper::CommandBuffer::Level::PRIMARY}).Value();
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

        m_SceneMeshes.push_back(std::move(VulkanHelper::Mesh::New(meshConfig).Value()));

        m_TotalVertexCount += mesh.Vertices.Size();
        m_TotalIndexCount += mesh.Indices.Size();
    }
    
    // Textures
    m_SceneTextures.clear();
    m_SceneTexturePathToIndex.clear();
    for (const auto& material : scene.Value().Materials)
    {
        uint64_t textureHash = 0;
        if (!material.BaseColorTextureFilepath.empty())
        {
            textureHash = std::hash<std::string>{}(material.BaseColorTextureFilepath);
            if (m_SceneTexturePathToIndex.find(textureHash) == m_SceneTexturePathToIndex.end())
            {
                m_SceneTexturePathToIndex[textureHash] = m_SceneTextures.size();
                m_SceneTextures.push_back(LoadTexture(material.BaseColorTextureFilepath, false, initializationCmd));
            }
        }
        else
        {
            // Load empty texture
            textureHash = std::hash<std::string>{}("EMPTY_BASECOLOR_TEXTURE");
            if (m_SceneTexturePathToIndex.find(textureHash) == m_SceneTexturePathToIndex.end())
            {
                m_SceneTexturePathToIndex[textureHash] = m_SceneTextures.size();
                m_SceneTextures.push_back(LoadDefaultTexture(initializationCmd, false, false));
            }
        }

        if (!material.NormalTextureFilepath.empty())
        {
            textureHash = std::hash<std::string>{}(material.NormalTextureFilepath);
            if (m_SceneTexturePathToIndex.find(textureHash) == m_SceneTexturePathToIndex.end())
            {
                m_SceneTexturePathToIndex[textureHash] = m_SceneTextures.size();
                m_SceneTextures.push_back(LoadTexture(material.NormalTextureFilepath, false, initializationCmd));
            }
        }
        else
        {
            // Load empty texture
            textureHash = std::hash<std::string>{}("EMPTY_NORMAL_TEXTURE");
            if (m_SceneTexturePathToIndex.find(textureHash) == m_SceneTexturePathToIndex.end())
            {
                m_SceneTexturePathToIndex[textureHash] = m_SceneTextures.size();
                m_SceneTextures.push_back(LoadDefaultTexture(initializationCmd, true, false));
            }
        }

        if (!material.RoughnessTextureFilepath.empty())
        {
            textureHash = std::hash<std::string>{}(material.RoughnessTextureFilepath);
            if (m_SceneTexturePathToIndex.find(textureHash) == m_SceneTexturePathToIndex.end())
            {
                m_SceneTexturePathToIndex[textureHash] = m_SceneTextures.size();
                m_SceneTextures.push_back(LoadTexture(material.RoughnessTextureFilepath, true, initializationCmd));
            }
        }
        else
        {
            // Load empty texture
            textureHash = std::hash<std::string>{}("EMPTY_ROUGHNESS_TEXTURE");
            if (m_SceneTexturePathToIndex.find(textureHash) == m_SceneTexturePathToIndex.end())
            {
                m_SceneTexturePathToIndex[textureHash] = m_SceneTextures.size();
                m_SceneTextures.push_back(LoadDefaultTexture(initializationCmd, false, true));
            }
        }

        if (!material.MetallicTextureFilepath.empty())
        {
            textureHash = std::hash<std::string>{}(material.MetallicTextureFilepath);
            if (m_SceneTexturePathToIndex.find(textureHash) == m_SceneTexturePathToIndex.end())
            {
                m_SceneTexturePathToIndex[textureHash] = m_SceneTextures.size();
                m_SceneTextures.push_back(LoadTexture(material.MetallicTextureFilepath, true, initializationCmd));
            }
        }
        else
        {
            // Load empty texture
            textureHash = std::hash<std::string>{}("EMPTY_METALLIC_TEXTURE");
            if (m_SceneTexturePathToIndex.find(textureHash) == m_SceneTexturePathToIndex.end())
            {
                m_SceneTexturePathToIndex[textureHash] = m_SceneTextures.size();
                m_SceneTextures.push_back(LoadDefaultTexture(initializationCmd, false, true));
            }
        }

        if (!material.EmissiveTextureFilepath.empty())
        {
            textureHash = std::hash<std::string>{}(material.EmissiveTextureFilepath);
            if (m_SceneTexturePathToIndex.find(textureHash) == m_SceneTexturePathToIndex.end())
            {
                m_SceneTexturePathToIndex[textureHash] = m_SceneTextures.size();
                m_SceneTextures.push_back(LoadTexture(material.EmissiveTextureFilepath, false, initializationCmd));
            }
        }
        else
        {
            // Load empty texture
            textureHash = std::hash<std::string>{}("EMPTY_EMISSIVE_TEXTURE");
            if (m_SceneTexturePathToIndex.find(textureHash) == m_SceneTexturePathToIndex.end())
            {
                m_SceneTexturePathToIndex[textureHash] = m_SceneTextures.size();
                m_SceneTextures.push_back(LoadDefaultTexture(initializationCmd, false, false));
            }
        }
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

        // Texture indices
        if (!material.BaseColorTextureFilepath.empty())
        {
            uint64_t textureHash = std::hash<std::string>{}(material.BaseColorTextureFilepath);
            pathTracerMaterial.BaseColorTextureIndex = (m_SceneTexturePathToIndex.find(textureHash) != m_SceneTexturePathToIndex.end()) ? (uint32_t)m_SceneTexturePathToIndex[textureHash] : 0;
        }
        else
        {
            uint64_t textureHash = std::hash<std::string>{}("EMPTY_BASECOLOR_TEXTURE");
            pathTracerMaterial.BaseColorTextureIndex = (m_SceneTexturePathToIndex.find(textureHash) != m_SceneTexturePathToIndex.end()) ? (uint32_t)m_SceneTexturePathToIndex[textureHash] : 0;
        }

        if (!material.NormalTextureFilepath.empty())
        {
            uint64_t textureHash = std::hash<std::string>{}(material.NormalTextureFilepath);
            pathTracerMaterial.NormalTextureIndex = (m_SceneTexturePathToIndex.find(textureHash) != m_SceneTexturePathToIndex.end()) ? (uint32_t)m_SceneTexturePathToIndex[textureHash] : 0;
        }
        else
        {
            uint64_t textureHash = std::hash<std::string>{}("EMPTY_NORMAL_TEXTURE");
            pathTracerMaterial.NormalTextureIndex = (m_SceneTexturePathToIndex.find(textureHash) != m_SceneTexturePathToIndex.end()) ? (uint32_t)m_SceneTexturePathToIndex[textureHash] : 0;
        }

        if (!material.RoughnessTextureFilepath.empty())
        {
            uint64_t textureHash = std::hash<std::string>{}(material.RoughnessTextureFilepath);
            pathTracerMaterial.RoughnessTextureIndex = (m_SceneTexturePathToIndex.find(textureHash) != m_SceneTexturePathToIndex.end()) ? (uint32_t)m_SceneTexturePathToIndex[textureHash] : 0;
        }
        else
        {
            uint64_t textureHash = std::hash<std::string>{}("EMPTY_ROUGHNESS_TEXTURE");
            pathTracerMaterial.RoughnessTextureIndex = (m_SceneTexturePathToIndex.find(textureHash) != m_SceneTexturePathToIndex.end()) ? (uint32_t)m_SceneTexturePathToIndex[textureHash] : 0;
        }

        if (!material.MetallicTextureFilepath.empty())
        {
            uint64_t textureHash = std::hash<std::string>{}(material.MetallicTextureFilepath);
            pathTracerMaterial.MetallicTextureIndex = (m_SceneTexturePathToIndex.find(textureHash) != m_SceneTexturePathToIndex.end()) ? (uint32_t)m_SceneTexturePathToIndex[textureHash] : 0;
        }
        else
        {
            uint64_t textureHash = std::hash<std::string>{}("EMPTY_METALLIC_TEXTURE");
            pathTracerMaterial.MetallicTextureIndex = (m_SceneTexturePathToIndex.find(textureHash) != m_SceneTexturePathToIndex.end()) ? (uint32_t)m_SceneTexturePathToIndex[textureHash] : 0;
        }

        if (!material.EmissiveTextureFilepath.empty())
        {
            uint64_t textureHash = std::hash<std::string>{}(material.EmissiveTextureFilepath);
            pathTracerMaterial.EmissiveTextureIndex = (m_SceneTexturePathToIndex.find(textureHash) != m_SceneTexturePathToIndex.end()) ? (uint32_t)m_SceneTexturePathToIndex[textureHash] : 0;
        }
        else
        {
            uint64_t textureHash = std::hash<std::string>{}("EMPTY_EMISSIVE_TEXTURE");
            pathTracerMaterial.EmissiveTextureIndex = (m_SceneTexturePathToIndex.find(textureHash) != m_SceneTexturePathToIndex.end()) ? (uint32_t)m_SceneTexturePathToIndex[textureHash] : 0;
        }

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

    VulkanHelper::CommandBuffer computeCmd = m_CommandPoolCompute.AllocateCommandBuffer({ VulkanHelper::CommandBuffer::Level::PRIMARY }).Value();
    VH_ASSERT(computeCmd.BeginRecording(VulkanHelper::CommandBuffer::Usage::ONE_TIME_SUBMIT_BIT) == VulkanHelper::VHResult::OK, "Failed to begin recording initialization command buffer");

    VulkanHelper::Vector<VulkanHelper::BLAS> blasVector;
    blasVector.Reserve(m_SceneMeshes.size());
    VulkanHelper::Vector<glm::mat4> modelMatrices;
    VulkanHelper::Vector<uint32_t> materialAndMeshIndices;
    modelMatrices.Reserve(scene.Value().MeshInstances.Size());
    materialAndMeshIndices.Reserve(scene.Value().MeshInstances.Size() * 2);
    VulkanHelper::Vector<uint32_t> customIndices;
    customIndices.Reserve(scene.Value().MeshInstances.Size());
    uint32_t index = 0;

    VulkanHelper::Vector<VulkanHelper::BLAS::Config> blasConfigs;
    for (const auto& instance : scene.Value().MeshInstances)
    {
        customIndices.PushBack(index);
        index++;

        materialAndMeshIndices.PushBack(instance.MaterialIndex);
        materialAndMeshIndices.PushBack(instance.MeshIndex);
        VH_ASSERT(instance.MaterialIndex < m_Materials.size(), "Mesh instance has invalid material index!");

        blasConfigs.PushBack({});
        auto& blasConfig = blasConfigs.Back();
        blasConfig.Device = m_Device;

        blasConfig.VertexBuffers.PushBack(m_SceneMeshes[instance.MeshIndex].GetVertexBuffer());
        blasConfig.IndexBuffers.PushBack(m_SceneMeshes[instance.MeshIndex].GetIndexBuffer());

        blasConfig.VertexSize = sizeof(VulkanHelper::LoadedMeshVertex);
        blasConfig.EnableCompaction = true;

        modelMatrices.PushBack(glm::mat4(1.0f));
    }

    VulkanHelper::BLASBuilder blasBuilder = VulkanHelper::BLASBuilder::New({ .Device = m_Device }).Value();
    auto buildResult = blasBuilder.Build(blasConfigs.Data(), (uint32_t)blasConfigs.Size(), computeCmd);
    
    VulkanHelper::Vector<VulkanHelper::BLAS> blasList = Move(buildResult.Value());
    VH_ASSERT(blasBuilder.Compact(blasList, computeCmd) == VulkanHelper::VHResult::OK, "Failed to compact BLASes");

    UploadDataToBuffer(m_MaterialAndMeshIndicesBuffer, materialAndMeshIndices.Data(), (uint32_t)materialAndMeshIndices.Size() * sizeof(uint32_t), 0, initializationCmd);

    m_SceneTLAS = VulkanHelper::TLAS::New({
        m_Device,
        std::move(blasList),
        std::move(customIndices),
        modelMatrices.Data(),
        &computeCmd
    }).Value();

    VH_ASSERT(computeCmd.EndRecording() == VulkanHelper::VHResult::OK, "Failed to end recording compute command buffer");
    VH_ASSERT(computeCmd.SubmitAndWait() == VulkanHelper::VHResult::OK, "Failed to submit compute command buffer");

    // Create Output Image
    // Size of the output image is based on the Aspect ratio of the camera, so it has to be created when new scene is loaded
    const int initialRes = 1080;
    m_Width = (uint32_t)((float)initialRes * aspectRatio);
    m_Height = initialRes;
    CreateOutputImageView();

    VulkanHelper::ShaderStages allRTShadersStages = VulkanHelper::ShaderStages::RAYGEN_BIT | VulkanHelper::ShaderStages::CLOSEST_HIT_BIT | VulkanHelper::ShaderStages::MISS_BIT;

    // Create Descriptor set
    std::array<VulkanHelper::DescriptorSet::BindingDescription, 19> bindingDescriptions = {
        VulkanHelper::DescriptorSet::BindingDescription{0, 1, allRTShadersStages, VulkanHelper::DescriptorType::STORAGE_IMAGE},
        VulkanHelper::DescriptorSet::BindingDescription{1, 1, allRTShadersStages, VulkanHelper::DescriptorType::ACCELERATION_STRUCTURE_KHR},
        VulkanHelper::DescriptorSet::BindingDescription{2, 1, allRTShadersStages, VulkanHelper::DescriptorType::UNIFORM_BUFFER},
        VulkanHelper::DescriptorSet::BindingDescription{3, (uint32_t)m_SceneMeshes.size(), allRTShadersStages, VulkanHelper::DescriptorType::STORAGE_BUFFER}, // vertex Meshes
        VulkanHelper::DescriptorSet::BindingDescription{4, (uint32_t)m_SceneMeshes.size(), allRTShadersStages, VulkanHelper::DescriptorType::STORAGE_BUFFER}, // index Meshes
        VulkanHelper::DescriptorSet::BindingDescription{5, (uint32_t)m_SceneTextures.size(), allRTShadersStages, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // Textures
        VulkanHelper::DescriptorSet::BindingDescription{6, 1, allRTShadersStages, VulkanHelper::DescriptorType::SAMPLER}, // Sampler
        VulkanHelper::DescriptorSet::BindingDescription{7, 1, allRTShadersStages, VulkanHelper::DescriptorType::STORAGE_BUFFER}, // Materials
        VulkanHelper::DescriptorSet::BindingDescription{8, 1, allRTShadersStages, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // Reflection Lookup
        VulkanHelper::DescriptorSet::BindingDescription{9, 1, allRTShadersStages, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // RefractionHitFromOutside Lookup
        VulkanHelper::DescriptorSet::BindingDescription{10, 1, allRTShadersStages, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // ReflectionHitFromInside Lookup
        VulkanHelper::DescriptorSet::BindingDescription{11, 1, allRTShadersStages, VulkanHelper::DescriptorType::SAMPLED_IMAGE}, // Env map
        VulkanHelper::DescriptorSet::BindingDescription{12, 1, allRTShadersStages, VulkanHelper::DescriptorType::STORAGE_BUFFER}, // Alias map
        VulkanHelper::DescriptorSet::BindingDescription{13, 1, allRTShadersStages, VulkanHelper::DescriptorType::STORAGE_BUFFER}, // Volumes buffer
        VulkanHelper::DescriptorSet::BindingDescription{14, 1, allRTShadersStages, VulkanHelper::DescriptorType::SAMPLER}, // Lookup sampler
        VulkanHelper::DescriptorSet::BindingDescription{15, MAX_HETEROGENEOUS_VOLUMES, allRTShadersStages, VulkanHelper::DescriptorType::STORAGE_BUFFER}, // Volume density buffers
        VulkanHelper::DescriptorSet::BindingDescription{16, MAX_HETEROGENEOUS_VOLUMES, allRTShadersStages, VulkanHelper::DescriptorType::STORAGE_BUFFER}, // Volume Temperature buffers
        VulkanHelper::DescriptorSet::BindingDescription{17, MAX_HETEROGENEOUS_VOLUMES, allRTShadersStages, VulkanHelper::DescriptorType::STORAGE_BUFFER}, // Volume max densities buffers
        VulkanHelper::DescriptorSet::BindingDescription{18, 1, allRTShadersStages, VulkanHelper::DescriptorType::STORAGE_BUFFER} // Instances material indices
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
    for (uint32_t i = 0; i < m_SceneTextures.size(); ++i)
    {
        VH_ASSERT(m_PathTracerDescriptorSet.AddImage(5, i, &m_SceneTextures[i], VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add albedo texture to descriptor set");
    }

    VH_ASSERT(m_PathTracerDescriptorSet.AddSampler(6, 0, &m_TextureSampler) == VulkanHelper::VHResult::OK, "Failed to add texture sampler to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(7, 0, &m_MaterialsBuffer) == VulkanHelper::VHResult::OK, "Failed to add materials buffer to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(8, 0, &m_ReflectionLookup, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add reflection lookup texture to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(9, 0, &m_RefractionFromOutsideLookup, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add refraction hit from outside lookup texture to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(10, 0, &m_RefractionFromInsideLookup, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add reflection hit from inside lookup texture to descriptor set");

    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(11, 0, &m_EnvMapTexture, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add env map texture to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(12, 0, &m_EnvAliasMap) == VulkanHelper::VHResult::OK, "Failed to add env alias map buffer to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(13, 0, &m_VolumesBuffer) == VulkanHelper::VHResult::OK, "Failed to add volumes buffer to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddSampler(14, 0, &m_LookupTableSampler) == VulkanHelper::VHResult::OK, "Failed to add lookup table sampler to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(18, 0, &m_MaterialAndMeshIndicesBuffer) == VulkanHelper::VHResult::OK, "Failed to add instances material indices buffer to descriptor set");

    // Upload Path Tracer uniform data
    PathTracerUniform pathTracerUniform{};
    m_CameraProjectionInverse = glm::inverse(glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f));
    pathTracerUniform.CameraViewInverse = m_CameraViewInverse;
    pathTracerUniform.CameraProjectionInverse = m_CameraProjectionInverse;
    pathTracerUniform.PlanetPosition = glm::vec4(m_PlanetPosition, 0.0f);
    pathTracerUniform.PlanetRadius = m_PlanetRadius;
    pathTracerUniform.AtmosphereHeight = m_AtmosphereHeight;
    pathTracerUniform.RayleighScatteringCoefficientMultiplier = glm::vec4(m_RayleighScatteringCoefficientMultiplier, 0.0f);
    pathTracerUniform.MieScatteringCoefficientMultiplier = glm::vec4(m_MieScatteringCoefficientMultiplier, 0.0f);
    pathTracerUniform.OzoneAbsorptionCoefficientMultiplier = glm::vec4(m_OzoneAbsorptionCoefficientMultiplier, 0.0f);
    pathTracerUniform.SunColor = glm::vec4(m_SunColor, 0.0f);
    pathTracerUniform.RayleighDensityFalloff = m_RayleighDensityFalloff;
    pathTracerUniform.MieDensityFalloff = m_MieDensityFalloff;
    pathTracerUniform.OzoneDensityFalloff = m_OzoneDensityFalloff;
    pathTracerUniform.OzonePeak = m_OzonePeak;
    pathTracerUniform.MaxDepth = m_MaxDepth;
    pathTracerUniform.SampleCount = m_SamplesPerFrame;
    pathTracerUniform.MaxLuminance = m_MaxLuminance;
    pathTracerUniform.FocusDistance = m_FocusDistance;
    pathTracerUniform.DepthOfFieldStrength = m_DepthOfFieldStrength;
    pathTracerUniform.SkyRotationAzimuth = m_SkyRotationAzimuth;
    pathTracerUniform.SkyRotationAltitude = m_SkyRotationAltitude;
    pathTracerUniform.VolumesCount = 0; // Starts empty
    pathTracerUniform.SkyIntensity = m_SkyIntensity;
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
        defines.push_back({"ENABLE_SKY_MIS", "1"});
    if (m_ShowEnvMapDirectly)
        defines.push_back({"SHOW_ENV_MAP_DIRECTLY", "1"});
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

    VulkanHelper::Shader::InitializeSession("../../PathTracer/Shaders/", (uint32_t)defines.size(), defines.data());
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

VulkanHelper::ImageView PathTracer::LoadTexture(const std::string& filePath, bool onlySingleChannel, VulkanHelper::CommandBuffer commandBuffer)
{
    VulkanHelper::AssetImporter importer = VulkanHelper::AssetImporter::New({m_ThreadPool}).Value();
    VulkanHelper::TextureAsset textureAsset = importer.ImportTexture(filePath).get().Value();

    std::vector<uint8_t> packedData;

    VulkanHelper::Image::Config imageConfig{};
    imageConfig.Device = m_Device;
    imageConfig.Width = textureAsset.Width;
    imageConfig.Height = textureAsset.Height;
    imageConfig.Format = VulkanHelper::Format::R8G8B8A8_UNORM;
    imageConfig.Usage = VulkanHelper::Image::Usage::SAMPLED_BIT | VulkanHelper::Image::Usage::TRANSFER_DST_BIT;

    if (onlySingleChannel)
    {
        packedData.resize(textureAsset.Data.Size() / 4);

        for (size_t i = 0; i < textureAsset.Data.Size(); i += 4)
        {
            packedData[i / 4] = textureAsset.Data[i]; // Take the R channel
        }

        imageConfig.Format = VulkanHelper::Format::R8_UNORM;
    }

    VulkanHelper::Image textureImage = VulkanHelper::Image::New(imageConfig).Value();

    // Create a staging buffer
    VulkanHelper::Buffer::Config stagingBufferConfig{};
    stagingBufferConfig.Device = m_Device;
    stagingBufferConfig.Size = onlySingleChannel ? packedData.size() * sizeof(uint8_t) : textureAsset.Data.Size() * sizeof(uint8_t);
    stagingBufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
    stagingBufferConfig.CpuMapable = true;
    stagingBufferConfig.DebugName = "Texture Staging Buffer";

    VulkanHelper::Buffer stagingBuffer = VulkanHelper::Buffer::New(stagingBufferConfig).Value();
    VH_ASSERT(stagingBuffer.UploadData(
        onlySingleChannel ? packedData.data() : textureAsset.Data.Data(),
        onlySingleChannel ? packedData.size() * sizeof(uint8_t) : textureAsset.Data.Size() * sizeof(uint8_t),
        0
    ) == VulkanHelper::VHResult::OK, "Failed to upload texture data");

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
            (uint32_t)i
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

// Creates a staging buffer and uploads data to the uniform buffer using the provided command buffer
void PathTracer::UploadDataToBuffer(VulkanHelper::Buffer buffer, void* data, uint64_t size, uint64_t offset, VulkanHelper::CommandBuffer& commandBuffer, bool deleteStageAfterUpload)
{
    const uint64_t maxSize = 100 * 1024 * 1024; // 100 MB
    
    // If data exceeds max size, split the upload into multiple smaller uploads
    if (size > maxSize)
    {
        uint64_t uploaded = 0;
        while (uploaded < size)
        {
            uint64_t chunkSize = std::min(maxSize, size - uploaded);
            UploadDataToBuffer(buffer, (uint8_t*)data + uploaded, chunkSize, offset + uploaded, commandBuffer, true);
            uploaded += chunkSize;
        }
        return;
    }

    // Create a staging buffer to upload uniform data
    VulkanHelper::Buffer::Config stagingBufferConfig{};
    stagingBufferConfig.Device = m_Device;
    stagingBufferConfig.Size = size;
    stagingBufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_SRC_BIT;
    stagingBufferConfig.CpuMapable = true;
    if (deleteStageAfterUpload)
        stagingBufferConfig.DeleteDelayInFrames = 0; // Delete immediately after going out of scope
    VulkanHelper::Buffer stagingBuffer = VulkanHelper::Buffer::New(stagingBufferConfig).Value();

    VH_ASSERT(stagingBuffer.UploadData(data, size, 0) == VulkanHelper::VHResult::OK, "Failed to upload path tracer uniform data");
    VH_ASSERT(buffer.CopyFromBuffer(commandBuffer, stagingBuffer, 0, offset, size) == VulkanHelper::VHResult::OK, "Failed to copy path tracer uniform buffer");

    if (deleteStageAfterUpload)
    {
        VH_ASSERT(commandBuffer.EndRecording() == VulkanHelper::VHResult::OK, "Failed to end recording command buffer");
        VH_ASSERT(commandBuffer.SubmitAndWait() == VulkanHelper::VHResult::OK, "Failed to submit command buffer");
        VH_ASSERT(commandBuffer.BeginRecording(VulkanHelper::CommandBuffer::Usage::ONE_TIME_SUBMIT_BIT) == VulkanHelper::VHResult::OK, "Failed to begin recording command buffer");
    }
    else
    {
        buffer.Barrier(
            commandBuffer,
            VulkanHelper::AccessFlags::TRANSFER_WRITE_BIT,
            VulkanHelper::AccessFlags::MEMORY_READ_BIT,
            VulkanHelper::PipelineStages::TRANSFER_BIT,
            VulkanHelper::PipelineStages::RAY_TRACING_SHADER_BIT_KHR
        );
    }
}

void PathTracer::DownloadDataFromBuffer(VulkanHelper::Buffer buffer, void* data, uint64_t size, uint64_t offset, VulkanHelper::CommandBuffer& commandBuffer)
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
    VH_ASSERT(m_PathTracerDescriptorSet.AddImage(11, 0, &m_EnvMapTexture, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL) == VulkanHelper::VHResult::OK, "Failed to add env map texture to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(12, 0, &m_EnvAliasMap) == VulkanHelper::VHResult::OK, "Failed to add env alias map buffer to descriptor set");
    ResetPathTracing();
}

void PathTracer::SetSkyAzimuth(float azimuth, VulkanHelper::CommandBuffer commandBuffer)
{
    m_SkyRotationAzimuth = azimuth;
    UploadDataToBuffer(m_PathTracerUniformBuffer, &azimuth, sizeof(float), offsetof(PathTracerUniform, SkyRotationAzimuth), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetSkyAltitude(float altitude, VulkanHelper::CommandBuffer commandBuffer)
{
    m_SkyRotationAltitude = altitude;
    UploadDataToBuffer(m_PathTracerUniformBuffer, &altitude, sizeof(float), offsetof(PathTracerUniform, SkyRotationAltitude), commandBuffer);
    ResetPathTracing();
}

void PathTracer::ReloadShaders(VulkanHelper::CommandBuffer& commandBuffer)
{
    std::vector<VulkanHelper::Shader::Define> defines;

    if (m_EnableEnvMapMIS)
        defines.push_back({"ENABLE_SKY_MIS", "1"});
    if (m_ShowEnvMapDirectly)
        defines.push_back({"SHOW_ENV_MAP_DIRECTLY", "1"});
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

    VulkanHelper::Shader::InitializeSession("../../PathTracer/Shaders/", (uint32_t)defines.size(), defines.data());
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

    textureImage.TransitionImageLayout(VulkanHelper::Image::Layout::TRANSFER_DST_OPTIMAL, commandBuffer);

    VulkanHelper::ImageView::Config imageViewConfig{};
    imageViewConfig.image = textureImage;
    imageViewConfig.ViewType = VulkanHelper::ImageView::ViewType::VIEW_2D;
    imageViewConfig.BaseLayer = 0;
    imageViewConfig.LayerCount = 1;

    m_EnvMapTexture = VulkanHelper::ImageView::New(imageViewConfig).Value();

    const uint32_t width = textureAsset.Width;
    const uint32_t height = textureAsset.Height;
    const uint64_t size = width * height;
    float* pixels = (float*)textureAsset.Data.Data(); // TODO wrong alignemnt

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

    textureImage.TransitionImageLayout(VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL, commandBuffer);

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
    VolumeGPU volumeGPU(volume);

    UploadDataToBuffer(m_VolumesBuffer, &volumeGPU, sizeof(VolumeGPU), (uint32_t)m_Volumes.size() * sizeof(VolumeGPU), commandBuffer);
    m_Volumes.push_back(volume);

    // Update Uniform Buffer
    uint32_t count = (uint32_t)m_Volumes.size();
    UploadDataToBuffer(m_PathTracerUniformBuffer, &count, sizeof(uint32_t), offsetof(PathTracerUniform, VolumesCount), commandBuffer);
    ResetPathTracing();
}

void PathTracer::AddDensityDataToVolume(uint32_t volumeIndex, const std::string& filepath, VulkanHelper::CommandBuffer commandBuffer)
{
    if (volumeIndex >= m_Volumes.size())
    {
        VH_LOG_ERROR("Volume index out of range: {}/{}", volumeIndex, m_Volumes.size());
        return;
    }
    auto& volume = m_Volumes[volumeIndex];

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
    VH_ASSERT(densityGrid != nullptr, "Density grid not found in VDB file. Volumes without density grid are not supported yet.");

    openvdb::FloatGrid::Ptr floatGridDensity = openvdb::gridPtrCast<openvdb::FloatGrid>(densityGrid);
    
    // Precompute max densities for empty space skipping
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

    volume.CornerMin = glm::vec3(min.x(), min.y(), min.z());
    volume.CornerMax = glm::vec3(max.x(), max.y(), max.z());

    // // Scale it down so AABB is more or less -1 to 1
    float maxDimX = glm::max(glm::abs(min.x()), glm::abs(max.x()));
    float maxDimY = glm::max(glm::abs(min.y()), glm::abs(max.y()));
    float maxDimZ = glm::max(glm::abs(min.z()), glm::abs(max.z()));
    float maxDim = glm::max(maxDimX, glm::max(maxDimY, maxDimZ));
    volume.CornerMin /= maxDim;
    volume.CornerMax /= maxDim;

    // For each volume there is 32x32x32 grid of max densities precomputed for empty space skipping
    std::array<float, 32768> volumeMaxDensities;
    volumeMaxDensities.fill(0.0f);

    // Prepare max densities and normalize temperature grid if present
    for (int z = 0; z < dim.z(); z++)
    {
        for (int y = 0; y < dim.y(); y++)
        {
            for (int x = 0; x < dim.x(); x++)
            {
                openvdb::math::Coord coord(min.x() + x, min.y() + (dim.y() - 1 - y), min.z() + z); // Y has to be flipped for vulkan

                float density = floatGridDensity->tree().getValue(coord) / maxDensity; // Normalize to [0, 1]

                int maxDensityGridIndex = ((x * 32) / dim.x()) + ((y * 32) / dim.y()) * 32 + ((z * 32) / dim.z()) * 1024;
                if (volumeMaxDensities[(uint32_t)maxDensityGridIndex] < density)
                    volumeMaxDensities[(uint32_t)maxDensityGridIndex] = density;

                // If temperature grid is present, modify the values so that they are rescaled from 0 to 1
                if (temperatureGrid)
                {
                    float temperature = floatGridTemperature->tree().getValue(coord);

                    // Store normalized temperature
                    temperature = glm::max((temperature - minTemperature) / (maxTemperature - minTemperature), 0.0f); // Normalize to [0, 1]

                    // Set the value
                    if (temperature > 0.0f)
                        floatGridDensity->tree().setValue(coord, temperature);
                }
            }
        }
    }

    // Density
    {
        nanovdb::GridHandle<nanovdb::HostBuffer> nanoGridHandleDensity = nanovdb::tools::createNanoGrid(*floatGridDensity);

        VH_LOG_DEBUG("Density data size: {} MB", ((float)nanoGridHandleDensity.buffer().size()) / (1024.0f * 1024.0f));
        VH_LOG_DEBUG("Volume dimensions: x: {} y: {} z: {}", dim.x(), dim.y(), dim.z());
        VH_LOG_DEBUG("Max Density: {}", maxDensity);

        VulkanHelper::Buffer::Config bufferConfig{};
        bufferConfig.Device = m_Device;
        bufferConfig.Usage = VulkanHelper::Buffer::Usage::STORAGE_BUFFER_BIT | VulkanHelper::Buffer::Usage::TRANSFER_DST_BIT;
        bufferConfig.DebugName = "NanoVDB Density Grid";
        bufferConfig.Size = nanoGridHandleDensity.buffer().size();
        
        volume.VolumeNanoBufferDensity = VulkanHelper::Buffer::New(bufferConfig).Value();

        UploadDataToBuffer(volume.VolumeNanoBufferDensity, nanoGridHandleDensity.buffer().data(), (uint32_t)nanoGridHandleDensity.buffer().size(), 0, commandBuffer);
    }

    // Temperature
    nanovdb::GridHandle<nanovdb::HostBuffer> nanoGridHandleTemperature;
    if (temperatureGrid)
    {
        openvdb::FloatGrid::Ptr floatGridTemperature = openvdb::gridPtrCast<openvdb::FloatGrid>(temperatureGrid);
        nanoGridHandleTemperature = nanovdb::tools::createNanoGrid(*floatGridTemperature);

        VulkanHelper::Buffer::Config bufferConfig{};
        bufferConfig.Device = m_Device;
        bufferConfig.Usage = VulkanHelper::Buffer::Usage::STORAGE_BUFFER_BIT | VulkanHelper::Buffer::Usage::TRANSFER_DST_BIT;
        bufferConfig.Size = nanoGridHandleTemperature.buffer().size();
        bufferConfig.DebugName = "NanoVDB Temperature Grid";
        
        volume.VolumeNanoBufferTemperature = VulkanHelper::Buffer::New(bufferConfig).Value();

        VH_LOG_DEBUG("Uploading NanoVDB volume temperature data, size: {} MB", ((float)nanoGridHandleTemperature.buffer().size()) / (1024.0f * 1024.0f));

        UploadDataToBuffer(volume.VolumeNanoBufferTemperature, nanoGridHandleTemperature.buffer().data(), (uint32_t)nanoGridHandleTemperature.buffer().size(), 0, commandBuffer);
    }

    VulkanHelper::Buffer::Config bufferConfig{};
    bufferConfig.Device = m_Device;
    bufferConfig.Size = sizeof(float) * volumeMaxDensities.size();
    bufferConfig.Usage = VulkanHelper::Buffer::Usage::STORAGE_BUFFER_BIT | VulkanHelper::Buffer::Usage::TRANSFER_DST_BIT;
    bufferConfig.DebugName = "VolumeMaxDensities";

    volume.MaxDensitiesBuffer = VulkanHelper::Buffer::New(bufferConfig).Value();

    UploadDataToBuffer(volume.MaxDensitiesBuffer, volumeMaxDensities.data(), (uint32_t)bufferConfig.Size, 0, commandBuffer);
        
    static int densityDataIndex = 0;
    VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(15, (uint32_t)densityDataIndex, &volume.VolumeNanoBufferDensity) == VulkanHelper::VHResult::OK, "Failed to add volume density textures buffer to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(16, (uint32_t)densityDataIndex, temperatureGrid ? &volume.VolumeNanoBufferTemperature : nullptr) == VulkanHelper::VHResult::OK, "Failed to add volume temperature textures buffer to descriptor set");
    VH_ASSERT(m_PathTracerDescriptorSet.AddBuffer(17, (uint32_t)densityDataIndex, &volume.MaxDensitiesBuffer) == VulkanHelper::VHResult::OK, "Failed to add volume max densities buffer to descriptor set");
    volume.DensityDataIndex = densityDataIndex;
    densityDataIndex = (densityDataIndex + 1) % (int)MAX_HETEROGENEOUS_VOLUMES;

    SetVolume(volumeIndex, volume, commandBuffer);
}

void PathTracer::RemoveDensityDataFromVolume(uint32_t volumeIndex, VulkanHelper::CommandBuffer commandBuffer)
{
    auto& volume = m_Volumes[volumeIndex];
    volume.VolumeNanoBufferDensity = VulkanHelper::Buffer();
    volume.VolumeNanoBufferTemperature = VulkanHelper::Buffer();
    volume.DensityDataIndex = -1;
    volume.MaxDensitiesBuffer = VulkanHelper::Buffer();
    volume.CornerMin = glm::vec3(-1.0f);
    volume.CornerMax = glm::vec3(1.0f);
    SetVolume(volumeIndex, volume, commandBuffer);
}

void PathTracer::RemoveVolume(uint32_t index, VulkanHelper::CommandBuffer commandBuffer)
{
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
}

void PathTracer::SetVolume(uint32_t index, const Volume& volume, VulkanHelper::CommandBuffer commandBuffer)
{
    m_Volumes[index] = volume;
    VolumeGPU volumeGPU(volume);
    UploadDataToBuffer(m_VolumesBuffer, &volumeGPU, sizeof(VolumeGPU), index * sizeof(VolumeGPU), commandBuffer);
    ResetPathTracing();
}

VulkanHelper::ImageView PathTracer::LoadDefaultTexture(VulkanHelper::CommandBuffer commandBuffer, bool normal, bool onlySingleChannel)
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

    if (onlySingleChannel)
    {
        // Default white texture is (1.0) in R
        textureData.resize(1);
        textureData[0] = 255;
    }

    VulkanHelper::Image::Config imageConfig{};
    imageConfig.Device = m_Device;
    imageConfig.Width = 1;
    imageConfig.Height = 1;
    imageConfig.Format = onlySingleChannel ? VulkanHelper::Format::R8_UNORM : VulkanHelper::Format::R8G8B8A8_UNORM;
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

void PathTracer::SetSkyMIS(bool value, VulkanHelper::CommandBuffer commandBuffer)
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

void PathTracer::SetSkyIntensity(float environmentIntensity, VulkanHelper::CommandBuffer commandBuffer)
{
    m_SkyIntensity = environmentIntensity;
    UploadDataToBuffer(m_PathTracerUniformBuffer, &m_SkyIntensity, sizeof(float), offsetof(PathTracerUniform, SkyIntensity), commandBuffer);
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

void PathTracer::SetPlanetRadius(float radius, VulkanHelper::CommandBuffer commandBuffer)
{
    m_PlanetRadius = radius;
    UploadDataToBuffer(m_PathTracerUniformBuffer, &radius, sizeof(float), offsetof(PathTracerUniform, PlanetRadius), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetAtmosphereHeight(float height, VulkanHelper::CommandBuffer commandBuffer)
{
    m_AtmosphereHeight = height;
    UploadDataToBuffer(m_PathTracerUniformBuffer, &height, sizeof(float), offsetof(PathTracerUniform, AtmosphereHeight), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetRayleighScatteringCoefficientMultiplier(const glm::vec3& multiplier, VulkanHelper::CommandBuffer commandBuffer)
{
    m_RayleighScatteringCoefficientMultiplier = multiplier;
    UploadDataToBuffer(m_PathTracerUniformBuffer, (void*)&multiplier, sizeof(glm::vec3), offsetof(PathTracerUniform, RayleighScatteringCoefficientMultiplier), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetMieScatteringCoefficientMultiplier(const glm::vec3& multiplier, VulkanHelper::CommandBuffer commandBuffer)
{
    m_MieScatteringCoefficientMultiplier = multiplier;
    UploadDataToBuffer(m_PathTracerUniformBuffer, (void*)&multiplier, sizeof(glm::vec3), offsetof(PathTracerUniform, MieScatteringCoefficientMultiplier), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetOzoneAbsorptionCoefficientMultiplier(const glm::vec3& multiplier, VulkanHelper::CommandBuffer commandBuffer)
{
    m_OzoneAbsorptionCoefficientMultiplier = multiplier;
    UploadDataToBuffer(m_PathTracerUniformBuffer, (void*)&multiplier, sizeof(glm::vec3), offsetof(PathTracerUniform, OzoneAbsorptionCoefficientMultiplier), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetPlanetPosition(const glm::vec3& position, VulkanHelper::CommandBuffer commandBuffer)
{
    m_PlanetPosition = position;
    UploadDataToBuffer(m_PathTracerUniformBuffer, (void*)&position, sizeof(glm::vec3), offsetof(PathTracerUniform, PlanetPosition), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetRayleighDensityFalloff(float falloff, VulkanHelper::CommandBuffer commandBuffer)
{
    m_RayleighDensityFalloff = falloff;
    UploadDataToBuffer(m_PathTracerUniformBuffer, &falloff, sizeof(float), offsetof(PathTracerUniform, RayleighDensityFalloff), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetMieDensityFalloff(float falloff, VulkanHelper::CommandBuffer commandBuffer)
{
    m_MieDensityFalloff = falloff;
    UploadDataToBuffer(m_PathTracerUniformBuffer, &falloff, sizeof(float), offsetof(PathTracerUniform, MieDensityFalloff), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetOzoneDensityFalloff(float falloff, VulkanHelper::CommandBuffer commandBuffer)
{
    m_OzoneDensityFalloff = falloff;
    UploadDataToBuffer(m_PathTracerUniformBuffer, &falloff, sizeof(float), offsetof(PathTracerUniform, OzoneDensityFalloff), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetOzonePeak(float altitude, VulkanHelper::CommandBuffer commandBuffer)
{
    m_OzonePeak = altitude;
    UploadDataToBuffer(m_PathTracerUniformBuffer, &altitude, sizeof(float), offsetof(PathTracerUniform, OzonePeak), commandBuffer);
    ResetPathTracing();
}

void PathTracer::SetSunColor(const glm::vec3& color, VulkanHelper::CommandBuffer commandBuffer)
{
    m_SunColor = color;
    UploadDataToBuffer(m_PathTracerUniformBuffer, (void*)&color, sizeof(glm::vec3), offsetof(PathTracerUniform, SunColor), commandBuffer);
    ResetPathTracing();
}
