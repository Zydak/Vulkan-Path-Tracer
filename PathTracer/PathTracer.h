#pragma once

#include "VulkanHelper.h"

class PathTracer
{
public:
    struct Material
    {
        glm::vec3 BaseColor = glm::vec3(1.0f);
        glm::vec3 EmissiveColor = glm::vec3(0.0f);
        glm::vec3 MediumColor = glm::vec3(1.0f);
        glm::vec3 MediumEmissiveColor = glm::vec3(0.0f);
        float Metallic = 0.0f;
        float Roughness = 1.0f;
        float IOR = 1.5f;
        float Transmission = 0.0f;
        float Anisotropy = 0.0f;
        float AnisotropyRotation = 0.0f;

        float MediumDensity;
        float MediumAnisotropy;
    };

    [[nodiscard]] static PathTracer New(const VulkanHelper::Device& device, VulkanHelper::ThreadPool* threadPool);

    void SetScene(const std::string& sceneFilePath);

    // True when all samples were accumulated
    bool PathTrace(VulkanHelper::CommandBuffer& commandBuffer);

    void ResizeImage(uint32_t width, uint32_t height, VulkanHelper::CommandBuffer commandBuffer);

    void ReloadShaders(VulkanHelper::CommandBuffer& commandBuffer);

    [[nodiscard]] inline VulkanHelper::ImageView GetOutputImageView() const { return m_OutputImageView; }
    [[nodiscard]] inline VulkanHelper::Image GetOutputImage() const { return m_OutputImageView.GetImage(); }

    [[nodiscard]] inline const std::vector<Material>& GetMaterials() const { return m_Materials; }
    [[nodiscard]] inline const Material& GetMaterial(uint32_t index) const { return m_Materials[index]; }
    [[nodiscard]] inline const std::string& GetMaterialName(uint32_t index) const { return m_MaterialNames[index]; }
    void SetMaterial(uint32_t index, const Material& material, VulkanHelper::CommandBuffer commandBuffer);

    [[nodiscard]] inline std::string& GetBaseColorTextureName(uint32_t index) { return m_SceneBaseColorTextureNames[index]; }
    [[nodiscard]] inline std::string& GetNormalTextureName(uint32_t index) { return m_SceneNormalTextureNames[index]; }
    [[nodiscard]] inline std::string& GetRoughnessTextureName(uint32_t index) { return m_SceneRoughnessTextureNames[index]; }
    [[nodiscard]] inline std::string& GetMetallicTextureName(uint32_t index) { return m_SceneMetallicTextureNames[index]; }
    [[nodiscard]] inline std::string& GetEmissiveTextureName(uint32_t index) { return m_SceneEmissiveTextureNames[index]; }

    void SetBaseColorTexture(uint32_t index, std::string filePath, VulkanHelper::CommandBuffer commandBuffer);
    void SetNormalTexture(uint32_t index, std::string filePath, VulkanHelper::CommandBuffer commandBuffer);
    void SetRoughnessTexture(uint32_t index, std::string filePath, VulkanHelper::CommandBuffer commandBuffer);
    void SetMetallicTexture(uint32_t index, std::string filePath, VulkanHelper::CommandBuffer commandBuffer);
    void SetEmissiveTexture(uint32_t index, std::string filePath, VulkanHelper::CommandBuffer commandBuffer);

    [[nodiscard]] inline uint32_t GetSamplesAccumulated() const { return m_SamplesAccumulated; }
    [[nodiscard]] inline uint32_t GetSamplesPerFrame() const { return m_SamplesPerFrame; }
    [[nodiscard]] inline uint32_t GetMaxSamplesAccumulated() const { return m_MaxSamplesAccumulated; }
    [[nodiscard]] inline uint32_t GetMaxDepth() const { return m_MaxDepth; }
    [[nodiscard]] inline float GetMaxLuminance() const { return m_MaxLuminance; }
    [[nodiscard]] inline float GetFocusDistance() const { return m_FocusDistance; }
    [[nodiscard]] inline float GetDepthOfFieldStrength() const { return m_DepthOfFieldStrength; }

    void SetMaxSamplesAccumulated(uint32_t maxSamples);
    void SetMaxDepth(uint32_t maxDepth, VulkanHelper::CommandBuffer commandBuffer);
    void SetSamplesPerFrame(uint32_t samplesPerFrame, VulkanHelper::CommandBuffer commandBuffer);
    void SetMaxLuminance(float maxLuminance, VulkanHelper::CommandBuffer commandBuffer);
    void SetFocusDistance(float focusDistance, VulkanHelper::CommandBuffer commandBuffer);
    void SetDepthOfFieldStrength(float depthOfFieldStrength, VulkanHelper::CommandBuffer commandBuffer);

    void ResetPathTracing() { m_FrameCount = 0; m_SamplesAccumulated = 0; }

    private:
    void CreateOutputImageView();
    VulkanHelper::ImageView LoadTexture(const char* filePath, VulkanHelper::CommandBuffer commandBuffer);
    VulkanHelper::ImageView LoadLookupTable(const char* filepath, glm::uvec3 tableSize, VulkanHelper::CommandBuffer& commandBuffer);

    constexpr static uint32_t MAX_ENTITIES = 2048;
    uint32_t m_FrameCount = 0;
    uint32_t m_SamplesAccumulated = 0;
    uint32_t m_SamplesPerFrame = 1;
    uint32_t m_MaxSamplesAccumulated = 5000;
    uint32_t m_MaxDepth = 200;
    float m_MaxLuminance = 500.0f;
    float m_FocusDistance = 1.0f;
    float m_DepthOfFieldStrength = 0.0f;

    VulkanHelper::Device m_Device;

    VulkanHelper::ImageView m_OutputImageView;
    uint32_t m_Width;
    uint32_t m_Height;
    float m_FOV = 45.0f;
    
    std::vector<VulkanHelper::ImageView> m_SceneBaseColorTextures;
    std::vector<std::string> m_SceneBaseColorTextureNames;
    std::vector<VulkanHelper::ImageView> m_SceneNormalTextures;
    std::vector<std::string> m_SceneNormalTextureNames;
    std::vector<VulkanHelper::ImageView> m_SceneRoughnessTextures;
    std::vector<std::string> m_SceneRoughnessTextureNames;
    std::vector<VulkanHelper::ImageView> m_SceneMetallicTextures;
    std::vector<std::string> m_SceneMetallicTextureNames;
    std::vector<VulkanHelper::ImageView> m_SceneEmissiveTextures;
    std::vector<std::string> m_SceneEmissiveTextureNames;
    std::vector<VulkanHelper::Mesh> m_SceneMeshes;
    VulkanHelper::TLAS m_SceneTLAS;

    VulkanHelper::ImageView m_ReflectionLookup;
    VulkanHelper::ImageView m_RefractionFromOutsideLookup;
    VulkanHelper::ImageView m_RefractionFromInsideLookup;

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
        uint32_t SampleCount;
        uint32_t MaxDepth;
        float MaxLuminance;
        float FocusDistance;
        float DepthOfFieldStrength;
    };
    VulkanHelper::Buffer m_PathTracerUniformBuffer;

    std::vector<Material> m_Materials;
    std::vector<std::string> m_MaterialNames;
    VulkanHelper::Buffer m_MaterialsBuffer;

    VulkanHelper::Sampler m_TextureSampler;

    VulkanHelper::ThreadPool* m_ThreadPool;
};