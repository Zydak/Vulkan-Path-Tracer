#pragma once
#include "Vulture.h"

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
	Vulture::MaterialProperties Material;
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

	inline Vulture::Image* GetOutputImage() { return &m_PathTracingImage; };
	inline Vulture::Image* GetGBufferAlbedo() { return &m_GBufferAlbedo; };
	inline Vulture::Image* GetGBufferNormal() { return &m_GBufferNormal; };

	void Resize(VkExtent2D newSize);
	void SetScene(Vulture::Scene* scene);
	bool Render();
	void UpdateResources();

	void ResetFrameAccumulation();
	void RecreateRayTracingPipeline();

	inline Vulture::Buffer* GetMaterialsBuffer() { return &m_RayTracingMaterialsBuffer; };
	inline Vulture::Buffer* GetVolumesBuffer() { return &m_VolumesBuffer; };

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

		std::string HitShaderPath = "src/shaders/raytrace.rchit";
		std::string MissShaderPath = "src/shaders/rtmiss.slang";
		std::string RayGenShaderPath = "src/shaders/raytrace.rgen";
	};

private:
	Vulture::AccelerationStructure m_GeometryAS;
	Vulture::AccelerationStructure m_VolumesAS;
	Vulture::Scene* m_CurrentSceneRendered;
	uint64_t m_CurrentSamplesPerPixel = 0;

	// Output Image
	Vulture::Image m_PathTracingImage;
	VkExtent2D m_ViewportSize = { 900, 900 };

	// GBuffer
	Vulture::Image m_GBufferAlbedo;
	Vulture::Image m_GBufferNormal;

	// Descriptor buffers
	Vulture::Buffer m_GlobalSetBuffer;
	Vulture::Buffer m_RayTracingSetBuffer;
	Vulture::Buffer m_RayTracingMaterialsBuffer;
	Vulture::Buffer m_RayTracingMeshesBuffer;
	Vulture::Buffer m_RayTracingDoFBuffer;
	Vulture::Buffer m_VolumesBuffer;

	// Descriptors
	Vulture::DescriptorSet m_RayTracingDescriptorSet;
	Vulture::DescriptorSet m_GlobalDescriptorSets;
	
	// Ray Tracing Pipeline
	Vulture::Pipeline m_RtPipeline;
	bool m_RecreateRTPipeline = false;

	// Shader Binding Table
	Vulture::SBT m_SBT;

	// Push Contants
	Vulture::PushConstant<PushConstantGBuffer> m_PushContantGBuffer;
	Vulture::PushConstant<PushConstantRay> m_PushContantRayTrace;

	// DOF
	Vulture::Effect<PushConstantDOF> m_DOfVisualizer;

	// Lookup Table
	std::vector<std::vector<float>> m_ReflectionEnergyLookupTable;
	std::vector<std::vector<float>> m_RefractionEtaLessThan1EnergyLookupTable;
	std::vector<std::vector<float>> m_RefractionEtaGreaterThan1EnergyLookupTable;
	Vulture::Image m_ReflectionLookupTexture;
	Vulture::Image m_RefractionLookupTextureEtaLessThan1;
	Vulture::Image m_RefractionLookupTextureEtaGreaterThan1;
};