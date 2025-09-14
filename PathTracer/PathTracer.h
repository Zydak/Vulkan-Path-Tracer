#pragma once

#include "VulkanHelper.h"

#include <unordered_map>

class PathTracer
{
public:
    struct Material
    {
        glm::vec3 BaseColor = glm::vec3(1.0f);
        glm::vec3 EmissiveColor = glm::vec3(0.0f);
        glm::vec3 SpecularColor = glm::vec3(1.0f);
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

    struct Volume
    {
        // AABB
        glm::vec3 CornerMin = glm::vec3(-1.0f);
        glm::vec3 CornerMax = glm::vec3(1.0f);
        glm::vec3 Position = glm::vec3(0.0f);
        glm::vec3 Scale = glm::vec3(1.0f);

        glm::vec3 Color = glm::vec3(0.8f);
        glm::vec3 EmissiveColor = glm::vec3(0.0f);
        float Density = 1.0f;
        float Anisotropy = 0.0f;
        float Alpha = 1.0f;
        float DropletSize = 20.0f;

        int DensityDataIndex = -1; // -1 if homogeneous

        std::string DensityTextureFilepath;
        VulkanHelper::ImageView DensityTextureView;
        
        // Heterogeneous volumes are split into 4x4x4 regions for skipping empty space
        VulkanHelper::Buffer MaxDensitiesBuffer;
    };

    enum class PhaseFunction
    {
        HENYEY_GREENSTEIN = 0,
        DRAINE = 1,
        HENYEY_GREENSTEIN_PLUS_DRAINE = 2
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

    void SetBaseColorTexture(uint32_t index, const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer);
    void SetNormalTexture(uint32_t index, const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer);
    void SetRoughnessTexture(uint32_t index, const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer);
    void SetMetallicTexture(uint32_t index, const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer);
    void SetEmissiveTexture(uint32_t index, const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer);
    void SetEnvMapMIS(bool value, VulkanHelper::CommandBuffer commandBuffer);
    void SetEnvMapShownDirectly(bool value, VulkanHelper::CommandBuffer commandBuffer);
    void SetCameraViewInverse(const glm::mat4& view, VulkanHelper::CommandBuffer commandBuffer);
    void SetCameraProjectionInverse(const glm::mat4& projection, VulkanHelper::CommandBuffer commandBuffer);
    void SetPhaseFunction(PhaseFunction phaseFunction, VulkanHelper::CommandBuffer commandBuffer);

    [[nodiscard]] inline uint32_t GetSamplesAccumulated() const { return m_SamplesAccumulated; }
    [[nodiscard]] inline uint32_t GetSamplesPerFrame() const { return m_SamplesPerFrame; }
    [[nodiscard]] inline uint32_t GetMaxSamplesAccumulated() const { return m_MaxSamplesAccumulated; }
    [[nodiscard]] inline uint32_t GetMaxDepth() const { return m_MaxDepth; }
    [[nodiscard]] inline float GetMaxLuminance() const { return m_MaxLuminance; }
    [[nodiscard]] inline float GetFocusDistance() const { return m_FocusDistance; }
    [[nodiscard]] inline float GetDepthOfFieldStrength() const { return m_DepthOfFieldStrength; }
    [[nodiscard]] inline const std::string& GetEnvMapFilepath() const { return m_EnvMapFilepath; }
    [[nodiscard]] inline float GetEnvMapRotationAzimuth() const { return m_EnvMapRotationAzimuth; }
    [[nodiscard]] inline float GetEnvMapRotationAltitude() const { return m_EnvMapRotationAltitude; }
    [[nodiscard]] inline uint32_t GetVolumesCount() const { return (uint32_t)m_Volumes.size(); }
    [[nodiscard]] inline const std::vector<Volume>& GetVolumes() const { return m_Volumes; }
    [[nodiscard]] inline bool IsEnvMapMISEnabled() const { return m_EnableEnvMapMIS; }
    [[nodiscard]] inline bool IsEnvMapShownDirectly() const { return m_ShowEnvMapDirectly; }
    [[nodiscard]] inline uint64_t GetTotalVertexCount() const { return m_TotalVertexCount; }
    [[nodiscard]] inline uint64_t GetTotalIndexCount() const { return m_TotalIndexCount; }
    [[nodiscard]] inline bool UseOnlyGeometryNormals() const { return m_UseOnlyGeometryNormals; }
    [[nodiscard]] inline bool UseEnergyCompensation() const { return m_UseEnergyCompensation; }
    [[nodiscard]] inline bool IsInFurnaceTestMode() const { return m_FurnaceTestMode; }
    [[nodiscard]] inline float GetEnvironmentIntensity() const { return m_EnvironmentIntensity; }
    [[nodiscard]] inline bool UseRayQueries() const { return m_UseRayQueries; }
    [[nodiscard]] inline const glm::mat4& GetCameraViewInverse() const { return m_CameraViewInverse; }
    [[nodiscard]] inline const glm::mat4& GetCameraProjectionInverse() const { return m_CameraProjectionInverse; }
    [[nodiscard]] inline PhaseFunction GetPhaseFunction() const { return m_PhaseFunction; }
    [[nodiscard]] inline uint32_t GetSplitScreenCount() const { return m_ScreenChunkCount; }

    void SetMaxSamplesAccumulated(uint32_t maxSamples);
    void SetMaxDepth(uint32_t maxDepth, VulkanHelper::CommandBuffer commandBuffer);
    void SetSamplesPerFrame(uint32_t samplesPerFrame, VulkanHelper::CommandBuffer commandBuffer);
    void SetMaxLuminance(float maxLuminance, VulkanHelper::CommandBuffer commandBuffer);
    void SetFocusDistance(float focusDistance, VulkanHelper::CommandBuffer commandBuffer);
    void SetDepthOfFieldStrength(float depthOfFieldStrength, VulkanHelper::CommandBuffer commandBuffer);
    void SetEnvMapFilepath(const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer);
    void SetEnvMapAzimuth(float azimuth, VulkanHelper::CommandBuffer commandBuffer);
    void SetEnvMapAltitude(float altitude, VulkanHelper::CommandBuffer commandBuffer);
    void AddVolume(const Volume& volume, VulkanHelper::CommandBuffer commandBuffer);
    void RemoveVolume(uint32_t index, VulkanHelper::CommandBuffer commandBuffer);
    void SetVolume(uint32_t index, const Volume& volume, VulkanHelper::CommandBuffer commandBuffer);
    void SetUseOnlyGeometryNormals(bool useOnlyGeometryNormals, VulkanHelper::CommandBuffer commandBuffer);
    void SetUseEnergyCompensation(bool useEnergyCompensation, VulkanHelper::CommandBuffer commandBuffer);
    void SetFurnaceTestMode(bool furnaceTestMode, VulkanHelper::CommandBuffer commandBuffer);
    void SetEnvironmentIntensity(float environmentIntensity, VulkanHelper::CommandBuffer commandBuffer);
    void SetUseRayQueries(bool useRayQueries, VulkanHelper::CommandBuffer commandBuffer);
    void AddDensityDataToVolume(uint32_t volumeIndex, const std::string& filepath, VulkanHelper::CommandBuffer commandBuffer);
    void RemoveDensityDataFromVolume(uint32_t volumeIndex, VulkanHelper::CommandBuffer commandBuffer);
    void SetSplitScreenCount(uint32_t count, VulkanHelper::CommandBuffer commandBuffer);

    void ResetPathTracing() { m_FrameCount = 0; m_DispatchCount = 0; m_SamplesAccumulated = 0; }

private:
    void CreateOutputImageView();
    void LoadEnvironmentMap(const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer);
    VulkanHelper::ImageView LoadTexture(const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer);
    VulkanHelper::ImageView LoadLookupTable(const char* filepath, glm::uvec3 tableSize, VulkanHelper::CommandBuffer& commandBuffer);
    VulkanHelper::ImageView LoadDefaultTexture(VulkanHelper::CommandBuffer commandBuffer, bool normal);

    constexpr static uint32_t MAX_ENTITIES = 2048;
    constexpr static uint32_t MAX_HETEROGENEOUS_VOLUMES = 100;

    glm::mat4 m_CameraViewInverse = glm::mat4(1.0f);
    glm::mat4 m_CameraProjectionInverse = glm::mat4(1.0f);
    uint64_t m_DispatchCount = 0;
    uint32_t m_FrameCount = 0;
    uint32_t m_SamplesAccumulated = 0;
    uint32_t m_SamplesPerFrame = 1;
    uint32_t m_MaxSamplesAccumulated = 5000;
    uint32_t m_MaxDepth = 200;
    float m_MaxLuminance = 500.0f;
    float m_FocusDistance = 1.0f;
    float m_DepthOfFieldStrength = 0.0f;
    std::string m_EnvMapFilepath = "../../Assets/meadow_2_4k.hdr";
    float m_EnvMapRotationAzimuth = 0.0f;
    float m_EnvMapRotationAltitude = 0.0f;
    bool m_EnableEnvMapMIS = true;
    bool m_ShowEnvMapDirectly = true;
    bool m_UseOnlyGeometryNormals = false;
    bool m_UseEnergyCompensation = true;
    bool m_FurnaceTestMode = false;
    float m_EnvironmentIntensity = 1.0f;
    bool m_UseRayQueries = true;
    PhaseFunction m_PhaseFunction = PhaseFunction::HENYEY_GREENSTEIN;
    uint32_t m_ScreenChunkCount = 1;

    uint64_t m_TotalVertexCount = 0;
    uint64_t m_TotalIndexCount = 0;

    VulkanHelper::Device m_Device;

    VulkanHelper::ImageView m_OutputImageView;
    uint32_t m_Width;
    uint32_t m_Height;
    float m_FOV = 45.0f;

    VulkanHelper::ImageView m_EnvMapTexture;
    VulkanHelper::Buffer m_EnvAliasMap;

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
    VulkanHelper::ImageView m_GGXALookup;

    VulkanHelper::CommandPool m_CommandPool;

    VulkanHelper::Pipeline m_PathTracerPipeline;

    VulkanHelper::DescriptorPool m_DescriptorPool;
    VulkanHelper::DescriptorSet m_PathTracerDescriptorSet;

    struct PathTracerUniform
    {
        glm::mat4 CameraViewInverse;
        glm::mat4 CameraProjectionInverse;
        uint32_t SampleCount;
        uint32_t MaxDepth;
        float MaxLuminance;
        float FocusDistance;
        float DepthOfFieldStrength;
        float EnvMapRotationAzimuth;
        float EnvMapRotationAltitude;
        uint32_t VolumesCount;
        float EnvironmentIntensity;
        uint32_t ScreenChunkCount;
    };

    struct PushConstantData
    {
        uint32_t FrameCount;
        uint32_t Seed;
        uint32_t ChunkIndex;
    };
    VulkanHelper::Buffer m_PathTracerUniformBuffer;
    VulkanHelper::PushConstant m_PathTracerPushConstant;

    std::vector<Material> m_Materials;
    std::vector<std::string> m_MaterialNames;
    VulkanHelper::Buffer m_MaterialsBuffer;

    VulkanHelper::Sampler m_TextureSampler;
    VulkanHelper::Sampler m_LookupTableSampler;

    VulkanHelper::ThreadPool* m_ThreadPool;

    struct VolumeGPU
    {
        // AABB
        glm::vec3 CornerMin = glm::vec3(-1.0f);
        glm::vec3 CornerMax = glm::vec3(1.0f);

        glm::vec3 Color = glm::vec3(0.8f);
        glm::vec3 EmissiveColor = glm::vec3(0.0f);
        float Density = 1.0f;
        float Anisotropy = 0.0f;
        float Alpha = 1.0f;
        float DropletSize = 20.0f;

        int DensityDataIndex = -1; // -1 if homogeneous

        VolumeGPU() = default;

        VolumeGPU(const Volume& volume)
            : CornerMin(volume.CornerMin),
              CornerMax(volume.CornerMax),
              Color(volume.Color),
              EmissiveColor(volume.EmissiveColor),
              Density(volume.Density),
              Anisotropy(volume.Anisotropy),
              Alpha(volume.Alpha),
              DropletSize(volume.DropletSize),
              DensityDataIndex(volume.DensityDataIndex)
        {
            CornerMin = volume.Position + (volume.CornerMin * volume.Scale);
            CornerMax = volume.Position + (volume.CornerMax * volume.Scale);
        }
    };

    std::vector<Volume> m_Volumes;
    VulkanHelper::Buffer m_VolumesBuffer;
};