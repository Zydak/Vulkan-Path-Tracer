#pragma once

#include "VulkanHelper.h"

class PathTracer
{
public:
    [[nodiscard]] static PathTracer New(const VulkanHelper::Device& device);

    void SetScene(const std::string& sceneFilePath);
    void PathTrace(VulkanHelper::CommandBuffer& commandBuffer);

    void ReloadShaders();

    [[nodiscard]] inline VulkanHelper::ImageView GetOutputImageView() const { return m_OutputImageView; }
    [[nodiscard]] inline VulkanHelper::Image GetOutputImage() const { return m_OutputImageView.GetImage(); }

private:
    constexpr static uint32_t MAX_ENTITIES = 2048;

    VulkanHelper::Device m_Device;

    VulkanHelper::ImageView m_OutputImageView;
    uint32_t m_ResolutionPixels = 1000;
    float m_AspectRatio = 1.0f;
    float m_FOV = 45.0f;
    
    std::vector<VulkanHelper::ImageView> m_SceneAlbedoTextures;
    std::vector<VulkanHelper::ImageView> m_SceneNormalTextures;
    std::vector<VulkanHelper::ImageView> m_SceneRoughnessTextures;
    std::vector<VulkanHelper::ImageView> m_SceneMetallicTextures;
    std::vector<VulkanHelper::ImageView> m_SceneEmissiveTextures;
    std::vector<VulkanHelper::Mesh> m_SceneMeshes;
    VulkanHelper::TLAS m_SceneTLAS;

    VulkanHelper::CommandPool m_CommandPool;

    VulkanHelper::Pipeline m_PathTracerPipeline;

    VulkanHelper::DescriptorPool m_DescriptorPool;
    VulkanHelper::DescriptorSet m_PathTracerDescriptorSet;

    struct PathTracerUniform
    {
        glm::mat4 CameraViewInverse;
        glm::mat4 CameraProjectionInverse;
        uint32_t FrameCount;
        uint32_t Seed;
    };
    VulkanHelper::Buffer m_PathTracerUniformBuffer;

    VulkanHelper::Buffer m_MaterialsBuffer;

    VulkanHelper::Sampler m_TextureSampler;
};