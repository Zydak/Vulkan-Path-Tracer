#pragma once
#include "VulkanHelper.h"

struct GlobalUbo
{
	glm::mat4 ViewProjectionMat;
	glm::mat4 ViewInverse;
	glm::mat4 ProjInverse;
};

struct PushConstantRay
{
	uint64_t frame = 0;
	int maxDepth = 0;
	int SamplesPerFrame = 0;
	float EnvAzimuth = 0.0f;
	float EnvAltitude = 0.0f;

	float FocalLength = 0.0f;
	float DoFStrength = 0.0f;
	float AliasingJitter = 0.0f;
	float SuppressCausticsLuminance = 0.0f;
	int VolumesCount = 0;
};

struct PushConstantGBuffer
{
	glm::mat4 Model;
	VulkanHelper::MaterialProperties Material;
};

struct PushConstantDOF
{
	glm::mat4 VPInverse;

	float FocalPoint;
	float Near;
	float Far;
};

struct MeshAdresses
{
	uint64_t VertexAddress; // Address of the Vertex buffer
	uint64_t IndexAddress; // Address of the index buffer
};

class PathTracer
{
public:
	PathTracer() = default;

	void Init(VkExtent2D size);

	~PathTracer() = default;
	PathTracer(const PathTracer& other) = delete;
	PathTracer(PathTracer&& other) noexcept = delete;
	PathTracer& operator=(const PathTracer& other) = delete;
	PathTracer& operator=(PathTracer&& other) noexcept = delete;

	inline VulkanHelper::Image* GetOutputImage() { return &m_PathTracingImage; };
	inline VulkanHelper::Image* GetGBufferAlbedo() { return &m_GBufferAlbedo; };
	inline VulkanHelper::Image* GetGBufferNormal() { return &m_GBufferNormal; };

	void Resize(VkExtent2D newSize);
	void SetScene(VulkanHelper::Scene* scene);
	bool Render();
	void UpdateResources();

	void ResetFrameAccumulation();
	void RecreateRayTracingPipeline();

	inline VulkanHelper::Buffer* GetMaterialsBuffer() { return &m_RayTracingMaterialsBuffer; };
	inline VulkanHelper::Buffer* GetVolumesBuffer() { return &m_VolumesBuffer; };

	inline uint64_t GetSamplesAccumulated() { return m_CurrentSamplesPerPixel; };
	inline uint64_t GetFrame() { return m_PushContantRayTrace.GetDataPtr()->frame; };

private:

	// Initialization stuff
	void CreateDescriptorSets();
	void CreateRayTracingPipeline();
	void CreateShaderBindingTable();
	void CreateFramebuffers();
	void CreateRayTracingDescriptorSets();
	void CreateAccelerationStructure();

	// Camera ubo update
	void UpdateDescriptorSetsData();

	// Lookup table
	void BuildEnergyLookupTable();

public:
	// Draw Parameters
	struct DrawInfo
	{
		float DOFStrength = 0.0f;
		float FocalLength = 8.0f;
		bool VisualizedDOF = false;
		bool AutoDoF = false;
		float AliasingJitterStr = 1.0f;
		float CausticsSuppresionMaxLuminance = 500.0f;
		int TotalSamplesPerPixel = 15000;
		int RayDepth = 20;
		int SamplesPerFrame = 15;
		bool UseCausticsSuppresion = true;
		bool ShowSkybox = true;
		bool FurnaceTestMode = false;

		float EnvAzimuth = 0.0f;
		float EnvAltitude = 0.0f;
	};

private:
	VulkanHelper::AccelerationStructure m_GeometryAS;
	VulkanHelper::AccelerationStructure m_VolumesAS;
	VulkanHelper::Scene* m_CurrentSceneRendered;
	uint64_t m_CurrentSamplesPerPixel = 0;

	// Output Image
	VulkanHelper::Image m_PathTracingImage;
	VkExtent2D m_ViewportSize = { 900, 900 };

	// GBuffer
	VulkanHelper::Image m_GBufferAlbedo;
	VulkanHelper::Image m_GBufferNormal;

	// Descriptor buffers
	VulkanHelper::Buffer m_GlobalSetBuffer;
	VulkanHelper::Buffer m_RayTracingSetBuffer;
	VulkanHelper::Buffer m_RayTracingMaterialsBuffer;
	VulkanHelper::Buffer m_RayTracingMeshesBuffer;
	VulkanHelper::Buffer m_RayTracingDoFBuffer;
	VulkanHelper::Buffer m_VolumesBuffer;

	// Descriptors
	VulkanHelper::DescriptorSet m_RayTracingDescriptorSet;
	VulkanHelper::DescriptorSet m_GlobalDescriptorSets;
	
	// Ray Tracing Pipeline
	VulkanHelper::Pipeline m_RtPipeline;
	bool m_RecreateRTPipeline = false;

	// Shader Binding Table
	VulkanHelper::SBT m_SBT;

	// Push Contants
	VulkanHelper::PushConstant<PushConstantGBuffer> m_PushContantGBuffer;
	VulkanHelper::PushConstant<PushConstantRay> m_PushContantRayTrace;

	// DOF
	VulkanHelper::Effect<PushConstantDOF> m_DOfVisualizer;

	// Lookup Table
	std::vector<float> m_ReflectionEnergyLookupTable;
	std::vector<float> m_RefractionEtaLessThan1EnergyLookupTable;
	std::vector<float> m_RefractionEtaGreaterThan1EnergyLookupTable;
	VulkanHelper::Image m_ReflectionLookupTexture;
	VulkanHelper::Image m_RefractionLookupTextureEtaLessThan1;
	VulkanHelper::Image m_RefractionLookupTextureEtaGreaterThan1;
};