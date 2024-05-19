#pragma once
#include "pch.h"
#include <Vulture.h>

struct GlobalUbo
{
	glm::mat4 ViewProjectionMat;
	glm::mat4 ViewInverse;
	glm::mat4 ProjInverse;
};

struct InkPushConstant
{
	int NoiseCenterPixelWeight = 2;
	int NoiseSampleRange = 1;
	float LuminanceBias = 0.0f;
};

struct PosterizePushConstant
{
	int ColorCount = 4;
	float DitherSpread = 0.5f;
	int DitherSize = 1;

	glm::vec4 colors[8];
};

struct PushConstantRay
{
	glm::vec4 ClearColor;
	int64_t frame = -1;
	int maxDepth;
	int SamplesPerFrame;
	float EnvAzimuth;
	float EnvAltitude;

	float FocalLength;
	float DoFStrength;
	float AliasingJitter;
};

struct PushConstantGBuffer
{
	glm::mat4 Model;
	Vulture::Material Material;
};

struct MeshAdresses
{
	uint64_t VertexAddress; // Address of the Vertex buffer
	uint64_t IndexAddress; // Address of the index buffer
};

class SceneRenderer
{
public:
	SceneRenderer();
	~SceneRenderer();

	void CreateRayTracingDescriptorSets();
	void SetSkybox(Vulture::Entity& skyboxEntity);

	void RecreateResources();
	void UpdateResources();
	void Denoise();
	void SetCurrentScene(Vulture::Scene* scene);
	bool RayTrace(const glm::vec4& clearColor);
	void PostProcess();
	void ResetFrameAccumulation();

	inline Vulture::PushConstant<PushConstantRay>& GetRTPush() { return m_PushContantRayTrace; };
	inline Vulture::PushConstant<InkPushConstant>& GetInkPush() { return m_InkEffect.GetPush(); };
	inline Vulture::PushConstant<PosterizePushConstant>& GetPosterizePush() { return m_PosterizeEffect.GetPush(); };
	inline Vulture::Ref<Vulture::Buffer> GetMaterialBuffer() { return m_RayTracingMaterialsBuffer; };
	inline Vulture::Ref<Vulture::Buffer> GetMeshBuffer() { return m_RayTracingMeshesBuffer; };

	inline Vulture::Bloom* GetBloom() { return &m_Bloom; };

	inline void RecompileTonemapShader() { m_RecompileTonemapShader = true; }

	enum class PostProcessEffects
	{
		None, // Bloom And Tonemap
		Ink, // Bloom And Tonemap With Ink Effect
		Posterize
	};

	void SetCurrentPostProcessEffect(PostProcessEffects effect) { m_CurrentEffect = effect; }
	void UpdateCamera();

	inline VkDescriptorSet GetTonemappedDescriptor() { return m_ImGuiViewportDescriptorTonemapped; }
	inline VkDescriptorSet GetPathTraceDescriptor() { return m_ImGuiViewportDescriptorPathTracing; }
	inline VkDescriptorSet GetNormalDescriptor() { return m_ImGuiNormalDescriptor; }
	inline VkDescriptorSet GetAlbedoDescriptor() { return m_ImGuiAlbedoDescriptor; }
	inline VkDescriptorSet GetRoughnessDescriptor() { return m_ImGuiRoughnessDescriptor; }
	inline VkDescriptorSet GetEmissiveDescriptor() { return m_ImGuiEmissiveDescriptor; }

	struct DrawInfo;
	struct DrawFileInfo;

	inline DrawInfo& GetDrawInfo() { return m_DrawInfo; }

	inline void SetViewportContentSize(VkExtent2D val) { m_ViewportContentSize = val; }
	inline void SetViewportSize(VkExtent2D val) { m_ViewportSize = val; }
	inline VkExtent2D GetViewportContentSize() { return m_ViewportContentSize; }
	inline VkExtent2D GetViewportSize() { return m_ViewportSize; }

	inline Vulture::Ref<Vulture::Image>& GetTonemappedImage() { return m_TonemappedImage; };
	inline Vulture::Ref<Vulture::Image>& GetPathTraceImage() { return m_PathTracingImage; };

	inline uint64_t GetAccumulatedSamples() { return m_CurrentSamplesPerPixel; };
private:
	void RecreateRayTracingDescriptorSets();
	void DrawGBuffer();

	void CreateRenderPasses();
	void CreateDescriptorSets();
	void RecreateDescriptorSets();
	void CreatePipelines();
	void CreateRayTracingPipeline();
	void CreateShaderBindingTable();
	void CreateFramebuffers();
	void UpdateDescriptorSetsData();

	enum GBufferImage
	{
		Albedo,
		Normal,
		RoughnessMetallness,
		Emissive,
		Depth,
		Count
	};
	Vulture::Ref<Vulture::Framebuffer> m_GBufferFramebuffer;
	Vulture::Pipeline m_GBufferPipeline;

	Vulture::Ref<Vulture::Image> m_DenoisedImage;
	Vulture::Ref<Vulture::Image> m_PathTracingImage;

	// Descriptor buffers
	Vulture::Ref<Vulture::Buffer> m_GlobalSetBuffer;
	Vulture::Ref<Vulture::Buffer> m_RayTracingSetBuffer;
	Vulture::Ref<Vulture::Buffer> m_RayTracingMaterialsBuffer;
	Vulture::Ref<Vulture::Buffer> m_RayTracingMeshesBuffer;
	Vulture::Ref<Vulture::Buffer> m_RayTracingDoFBuffer;

	Vulture::Ref<Vulture::DescriptorSet> m_RayTracingDescriptorSet;
	std::vector<Vulture::Ref<Vulture::DescriptorSet>> m_GlobalDescriptorSets;
	Vulture::Pipeline m_RtPipeline;
	
	Vulture::Ref<Vulture::Image> m_PresentedImage;
	Vulture::Ref<Vulture::Image> m_TonemappedImage;
	Vulture::Ref<Vulture::Image> m_BloomImage;

	Vulture::AssetHandle m_BlueNoiseImage;
	Vulture::AssetHandle m_PaperTexture;
	Vulture::AssetHandle m_InkTexture;

	PostProcessEffects m_CurrentEffect = PostProcessEffects::None;

	Vulture::SBT m_SBT;

	Vulture::PushConstant<PushConstantGBuffer> m_PushContantGBuffer;
	Vulture::PushConstant<PushConstantRay> m_PushContantRayTrace;

	Vulture::Scene* m_CurrentSceneRendered;

	VkFence m_DenoiseFence;
	uint64_t m_DenoiseFenceValue = 0U;
	Vulture::Ref<Vulture::Denoiser> m_Denoiser;
	Vulture::Tonemap m_DenoisedTonemapper;
	Vulture::Bloom m_Bloom;
	Vulture::Bloom m_DenoisedBloom;

	Vulture::Tonemap m_Tonemapper;
	bool m_RecompileTonemapShader = false;

	Vulture::Effect<InkPushConstant> m_InkEffect;
	Vulture::Effect<PosterizePushConstant> m_PosterizeEffect;

	// ImGui Stuff / Interface
	uint64_t m_CurrentSamplesPerPixel = 0;
	VkDescriptorSet m_ImGuiViewportDescriptorTonemapped;
	VkDescriptorSet m_ImGuiViewportDescriptorPathTracing;
	VkDescriptorSet m_ImGuiNormalDescriptor;
	VkDescriptorSet m_ImGuiAlbedoDescriptor;
	VkDescriptorSet m_ImGuiRoughnessDescriptor;
	VkDescriptorSet m_ImGuiEmissiveDescriptor;
	VkExtent2D m_ViewportSize = { 1920, 1080 };
	VkExtent2D m_ViewportContentSize = { 1920, 1080 };

	bool m_DrawGBuffer     = true;
	bool m_HasEnvMap	   = false;

public:
	struct DrawInfo
	{
		float DOFStrength			= 0.0f;
		float FocalLength			= 8.0f;
		bool AutoDoF				= false;
		float AliasingJitterStr		= 1.0f;
		int TotalSamplesPerPixel	= 15000;
		int RayDepth				= 20;
		int SamplesPerFrame			= 15;
		bool UseNormalMaps			= false;
		bool UseAlbedo				= true;
		bool UseGlossy				= true;
		bool UseGlass				= true;
		bool UseClearcoat			= true;
		bool UseFireflies			= true;
		bool ShowSkybox				= true;

		bool UseTestShaders			= false;
		bool UseCosineWeight		= true;

		bool SampleEnvMap   = true;
		float EnvAzimuth	= 0.0f;
		float EnvAltitude	= 0.0f;

		Vulture::Tonemap::TonemapInfo TonemapInfo{};
		Vulture::Bloom::BloomInfo BloomInfo{};
	};
private:
	DrawInfo m_DrawInfo{};
};