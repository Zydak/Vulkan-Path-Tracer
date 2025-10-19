#pragma once

#include "Vulkan/CommandPool.h"
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

        float MediumDensity = 0.0f;
        float MediumAnisotropy = 0.0f;

        uint32_t BaseColorTextureIndex = 0;
        uint32_t NormalTextureIndex = 0;
        uint32_t RoughnessTextureIndex = 0;
        uint32_t MetallicTextureIndex = 0;
        uint32_t EmissiveTextureIndex = 0;
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
        glm::vec3 TemperatureColor = glm::vec3(1.0f, 0.5f, 0.0f); // If blackbody is disabled
        float Density = 1.0f;
        float Anisotropy = 0.0f;
        float Alpha = 1.0f;
        float DropletSize = 20.0f;

        int DensityDataIndex = -1; // -1 if homogeneous

        int UseBlackbody = 1;
        int HasTemperatureData = 0;
        float TemperatureGamma = 1.0f; // Exponent for temperature to compute blackbody radiation
        float TemperatureScale = 1.0f; // Scales the temperature value read from the grid before using it to compute emission
        float EmissiveColorGamma = 1.0f; // Exponent for emissive color
        int KelvinMin = 500;
        int KelvinMax = 8000;

        // This enables faster but biased methods for rendering clouds
        int ApproximatedScatteringForClouds = 0;

        VulkanHelper::Buffer VolumeNanoBufferDensity;
        VulkanHelper::Buffer VolumeNanoBufferTemperature;

        // Heterogeneous volumes are split into 32x32x32 regions for skipping empty space
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

    void ResizeImage(uint32_t width, uint32_t height);

    void ReloadShaders(VulkanHelper::CommandBuffer& commandBuffer);

    [[nodiscard]] inline VulkanHelper::ImageView GetOutputImageView() const { return m_OutputImageView; }
    [[nodiscard]] inline VulkanHelper::Image GetOutputImage() const { return m_OutputImageView.GetImage(); }

    [[nodiscard]] inline const std::vector<Material>& GetMaterials() const { return m_Materials; }
    [[nodiscard]] inline const Material& GetMaterial(uint32_t index) const { return m_Materials[index]; }
    [[nodiscard]] inline const std::string& GetMaterialName(uint32_t index) const { return m_MaterialNames[index]; }
    void SetMaterial(uint32_t index, const Material& material, VulkanHelper::CommandBuffer commandBuffer);

    void SetSkyMIS(bool value, VulkanHelper::CommandBuffer commandBuffer);
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
    [[nodiscard]] inline float GetSkyRotationAzimuth() const { return m_SkyRotationAzimuth; }
    [[nodiscard]] inline float GetSkyRotationAltitude() const { return m_SkyRotationAltitude; }
    [[nodiscard]] inline uint32_t GetVolumesCount() const { return (uint32_t)m_Volumes.size(); }
    [[nodiscard]] inline const std::vector<Volume>& GetVolumes() const { return m_Volumes; }
    [[nodiscard]] inline bool IsSkyMISEnabled() const { return m_EnableEnvMapMIS; }
    [[nodiscard]] inline bool IsEnvMapShownDirectly() const { return m_ShowEnvMapDirectly; }
    [[nodiscard]] inline uint64_t GetTotalVertexCount() const { return m_TotalVertexCount; }
    [[nodiscard]] inline uint64_t GetTotalIndexCount() const { return m_TotalIndexCount; }
    [[nodiscard]] inline bool UseOnlyGeometryNormals() const { return m_UseOnlyGeometryNormals; }
    [[nodiscard]] inline bool UseEnergyCompensation() const { return m_UseEnergyCompensation; }
    [[nodiscard]] inline bool IsInFurnaceTestMode() const { return m_FurnaceTestMode; }
    [[nodiscard]] inline float GetSkyIntensity() const { return m_SkyIntensity; }
    [[nodiscard]] inline bool UseRayQueries() const { return m_UseRayQueries; }
    [[nodiscard]] inline const glm::mat4& GetCameraViewInverse() const { return m_CameraViewInverse; }
    [[nodiscard]] inline const glm::mat4& GetCameraProjectionInverse() const { return m_CameraProjectionInverse; }
    [[nodiscard]] inline PhaseFunction GetPhaseFunction() const { return m_PhaseFunction; }
    [[nodiscard]] inline uint32_t GetSplitScreenCount() const { return m_ScreenChunkCount; }
    [[nodiscard]] inline bool IsAtmosphereEnabled() const { return m_EnableAtmosphere; }
    [[nodiscard]] inline const glm::vec3& GetPlanetPosition() const { return m_PlanetPosition; }
    [[nodiscard]] inline float GetPlanetRadius() const { return m_PlanetRadius; }
    [[nodiscard]] inline float GetAtmosphereHeight() const { return m_AtmosphereHeight; }
    [[nodiscard]] inline const glm::vec3& GetRayleighScatteringCoefficientMultiplier() const { return m_RayleighScatteringCoefficientMultiplier; }
    [[nodiscard]] inline const glm::vec3& GetMieScatteringCoefficientMultiplier() const { return m_MieScatteringCoefficientMultiplier; }
    [[nodiscard]] inline const glm::vec3& GetOzoneAbsorptionCoefficientMultiplier() const { return m_OzoneAbsorptionCoefficientMultiplier; }
    [[nodiscard]] inline float GetRayleighDensityFalloff() const { return m_RayleighDensityFalloff; }
    [[nodiscard]] inline float GetMieDensityFalloff() const { return m_MieDensityFalloff; }
    [[nodiscard]] inline float GetOzoneDensityFalloff() const { return m_OzoneDensityFalloff; }
    [[nodiscard]] inline float GetOzonePeak() const { return m_OzonePeak; }
    [[nodiscard]] inline const glm::vec3& GetSunColor() const { return m_SunColor; }

    void SetMaxSamplesAccumulated(uint32_t maxSamples);
    void SetMaxDepth(uint32_t maxDepth, VulkanHelper::CommandBuffer commandBuffer);
    void SetSamplesPerFrame(uint32_t samplesPerFrame, VulkanHelper::CommandBuffer commandBuffer);
    void SetMaxLuminance(float maxLuminance, VulkanHelper::CommandBuffer commandBuffer);
    void SetFocusDistance(float focusDistance, VulkanHelper::CommandBuffer commandBuffer);
    void SetDepthOfFieldStrength(float depthOfFieldStrength, VulkanHelper::CommandBuffer commandBuffer);
    void SetEnvMapFilepath(const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer);
    void SetSkyAzimuth(float azimuth, VulkanHelper::CommandBuffer commandBuffer);
    void SetSkyAltitude(float altitude, VulkanHelper::CommandBuffer commandBuffer);
    void AddVolume(const Volume& volume, VulkanHelper::CommandBuffer commandBuffer);
    void RemoveVolume(uint32_t index, VulkanHelper::CommandBuffer commandBuffer);
    void SetVolume(uint32_t index, const Volume& volume, VulkanHelper::CommandBuffer commandBuffer);
    void SetUseOnlyGeometryNormals(bool useOnlyGeometryNormals, VulkanHelper::CommandBuffer commandBuffer);
    void SetUseEnergyCompensation(bool useEnergyCompensation, VulkanHelper::CommandBuffer commandBuffer);
    void SetFurnaceTestMode(bool furnaceTestMode, VulkanHelper::CommandBuffer commandBuffer);
    void SetSkyIntensity(float skyIntensity, VulkanHelper::CommandBuffer commandBuffer);
    void SetUseRayQueries(bool useRayQueries, VulkanHelper::CommandBuffer commandBuffer);
    void AddDensityDataToVolume(uint32_t volumeIndex, const std::string& filepath, VulkanHelper::CommandBuffer commandBuffer);
    void RemoveDensityDataFromVolume(uint32_t volumeIndex, VulkanHelper::CommandBuffer commandBuffer);
    void SetSplitScreenCount(uint32_t count, VulkanHelper::CommandBuffer commandBuffer);
    void SetEnableAtmosphere(bool enable, VulkanHelper::CommandBuffer commandBuffer);
    void SetPlanetPosition(const glm::vec3& position, VulkanHelper::CommandBuffer commandBuffer);
    void SetPlanetRadius(float radius, VulkanHelper::CommandBuffer commandBuffer);
    void SetAtmosphereHeight(float height, VulkanHelper::CommandBuffer commandBuffer);
    void SetRayleighScatteringCoefficientMultiplier(const glm::vec3& multiplier, VulkanHelper::CommandBuffer commandBuffer);
    void SetMieScatteringCoefficientMultiplier(const glm::vec3& multiplier, VulkanHelper::CommandBuffer commandBuffer);
    void SetOzoneAbsorptionCoefficientMultiplier(const glm::vec3& multiplier, VulkanHelper::CommandBuffer commandBuffer);
    void SetRayleighDensityFalloff(float falloff, VulkanHelper::CommandBuffer commandBuffer);
    void SetMieDensityFalloff(float falloff, VulkanHelper::CommandBuffer commandBuffer);
    void SetOzoneDensityFalloff(float falloff, VulkanHelper::CommandBuffer commandBuffer);
    void SetOzonePeak(float peak, VulkanHelper::CommandBuffer commandBuffer);
    void SetSunColor(const glm::vec3& color, VulkanHelper::CommandBuffer commandBuffer);

    void ResetPathTracing() { m_FrameCount = 0; m_DispatchCount = 0; m_SamplesAccumulated = 0; }

private:
    void CreateOutputImageView();
    void LoadEnvironmentMap(const std::string& filePath, VulkanHelper::CommandBuffer commandBuffer);
    VulkanHelper::ImageView LoadTexture(const std::string& filePath, bool onlySingleChannel, VulkanHelper::CommandBuffer commandBuffer);
    VulkanHelper::ImageView LoadLookupTable(const char* filepath, glm::uvec3 tableSize, VulkanHelper::CommandBuffer& commandBuffer);
    VulkanHelper::ImageView LoadDefaultTexture(VulkanHelper::CommandBuffer commandBuffer, bool normal, bool onlySingleChannel);

    constexpr static uint32_t MAX_ENTITIES = 10000;
    constexpr static uint32_t MAX_INSTANCES = 100000;
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
    float m_SkyRotationAzimuth = 0.0f;
    float m_SkyRotationAltitude = 0.0f;
    bool m_EnableEnvMapMIS = true;
    bool m_ShowEnvMapDirectly = true;
    bool m_UseOnlyGeometryNormals = false;
    bool m_UseEnergyCompensation = true;
    bool m_FurnaceTestMode = false;
    float m_SkyIntensity = 1.0f;
    bool m_UseRayQueries = true;
    PhaseFunction m_PhaseFunction = PhaseFunction::HENYEY_GREENSTEIN;
    uint32_t m_ScreenChunkCount = 1;
    bool m_EnableAtmosphere = false;
    glm::vec3 m_PlanetPosition = glm::vec3(0.0f, 6360e3f + 1000.0f, 0.0f); // In meters
    float m_PlanetRadius = 6360e3f; // In meters
    float m_AtmosphereHeight = 100e3f; // In meters
    glm::vec3 m_RayleighScatteringCoefficientMultiplier = glm::vec3(1.0f);
    glm::vec3 m_MieScatteringCoefficientMultiplier = glm::vec3(1.0f);
    glm::vec3 m_OzoneAbsorptionCoefficientMultiplier = glm::vec3(1.0f);
    glm::vec3 m_SunColor = glm::vec3(1.0f, 0.956f, 0.88f);
    float m_RayleighDensityFalloff = 8000.0f; // In meters
    float m_MieDensityFalloff = 1200.0f; // In meters
    float m_OzoneDensityFalloff = 5000.0f; // In meters
    float m_OzonePeak = 22000.0f; // In meters

    uint64_t m_TotalVertexCount = 0;
    uint64_t m_TotalIndexCount = 0;

    VulkanHelper::Device m_Device;

    VulkanHelper::ImageView m_OutputImageView;
    uint32_t m_Width;
    uint32_t m_Height;

    VulkanHelper::ImageView m_EnvMapTexture;
    VulkanHelper::Buffer m_EnvAliasMap;

    std::vector<VulkanHelper::ImageView> m_SceneTextures;
    std::unordered_map<uint64_t, uint64_t> m_SceneTexturePathToIndex;
    std::vector<VulkanHelper::Mesh> m_SceneMeshes;
    VulkanHelper::TLAS m_SceneTLAS;

    VulkanHelper::ImageView m_ReflectionLookup;
    VulkanHelper::ImageView m_RefractionFromOutsideLookup;
    VulkanHelper::ImageView m_RefractionFromInsideLookup;
    VulkanHelper::ImageView m_GGXALookup;

    VulkanHelper::CommandPool m_CommandPoolGraphics;
    VulkanHelper::CommandPool m_CommandPoolCompute;

    VulkanHelper::Pipeline m_PathTracerPipeline;

    VulkanHelper::DescriptorPool m_DescriptorPool;
    VulkanHelper::DescriptorSet m_PathTracerDescriptorSet;

    struct PathTracerUniform
    {
        glm::mat4 CameraViewInverse;
        glm::mat4 CameraProjectionInverse;

        // Atmosphere parameters float4 to ensure alignment
        glm::vec4 PlanetPosition;
        glm::vec4 RayleighScatteringCoefficientMultiplier;
        glm::vec4 MieScatteringCoefficientMultiplier;
        glm::vec4 OzoneAbsorptionCoefficientMultiplier;
        glm::vec4 SunColor;
        float PlanetRadius;
        float AtmosphereHeight;
        float RayleighDensityFalloff;
        float MieDensityFalloff;
        float OzoneDensityFalloff;
        float OzonePeak;

        uint32_t SampleCount;
        uint32_t MaxDepth;
        float MaxLuminance;
        float FocusDistance;
        float DepthOfFieldStrength;
        float SkyRotationAzimuth;
        float SkyRotationAltitude;
        uint32_t VolumesCount;
        float SkyIntensity;
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

    void UploadDataToBuffer(VulkanHelper::Buffer buffer, void* data, uint64_t size, uint64_t offset, VulkanHelper::CommandBuffer& commandBuffer, bool deleteStageAfterUpload = false);
    void DownloadDataFromBuffer(VulkanHelper::Buffer buffer, void* data, uint64_t size, uint64_t offset, VulkanHelper::CommandBuffer& commandBuffer);

    std::vector<Material> m_Materials;
    std::vector<std::string> m_MaterialNames;
    VulkanHelper::Buffer m_MaterialsBuffer;
    VulkanHelper::Buffer m_MaterialAndMeshIndicesBuffer;

    VulkanHelper::Sampler m_TextureSampler;
    VulkanHelper::Sampler m_LookupTableSampler;

    VulkanHelper::ThreadPool* m_ThreadPool;

    // Data that gets sent to the GPU
    struct VolumeGPU
    {
        // AABB
        glm::vec3 CornerMin = glm::vec3(-1.0f);
        glm::vec3 CornerMax = glm::vec3(1.0f);

        glm::vec3 Color = glm::vec3(0.8f);
        glm::vec3 EmissiveColor = glm::vec3(0.0f);
        glm::vec3 TemperatureColor = glm::vec3(1.0f, 0.5f, 0.0f); // If blackbody is disabled
        float Density = 1.0f;
        float Anisotropy = 0.0f;
        float Alpha = 1.0f;
        float DropletSize = 20.0f;

        int DensityDataIndex = -1; // -1 if homogeneous

        int UseBlackbody = 1;
        int HasTemperatureData = 0;
        float TemperatureGamma = 1.0f; // Exponent for temperature to compute blackbody radiation
        float TemperatureScale = 1.0f; // Scales the temperature value read from the grid before using it to compute emission
        float EmissiveColorGamma = 1.0f; // Exponent for emissive color
        int KelvinMin = 500;
        int KelvinMax = 8000;

        // This enables faster but biased methods for rendering clouds
        int ApproximatedScatteringForClouds = 0;

        VolumeGPU() = default;

        VolumeGPU(const Volume& volume)
            : CornerMin(volume.CornerMin)
            , CornerMax(volume.CornerMax)
            , Color(volume.Color)
            , EmissiveColor(volume.EmissiveColor)
            , TemperatureColor(volume.TemperatureColor)
            , Density(volume.Density)
            , Anisotropy(volume.Anisotropy)
            , Alpha(volume.Alpha)
            , DropletSize(volume.DropletSize)
            , DensityDataIndex(volume.DensityDataIndex)
            , UseBlackbody(volume.UseBlackbody)
            , TemperatureGamma(volume.TemperatureGamma)
            , TemperatureScale(volume.TemperatureScale)
            , EmissiveColorGamma(volume.EmissiveColorGamma) // Default value
            , KelvinMin(volume.KelvinMin)
            , KelvinMax(volume.KelvinMax)
            , ApproximatedScatteringForClouds(volume.ApproximatedScatteringForClouds)
        {
            CornerMin = volume.Position + (volume.CornerMin * volume.Scale);
            CornerMax = volume.Position + (volume.CornerMax * volume.Scale);
            HasTemperatureData = volume.VolumeNanoBufferTemperature != nullptr;
        }
    };

    std::vector<Volume> m_Volumes;
    VulkanHelper::Buffer m_VolumesBuffer;
};