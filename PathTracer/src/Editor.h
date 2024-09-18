#pragma once

#include "pch.h"

#include "PathTracer.h"
#include "PostProcessor.h"

class Vulture::Scene;

class Editor
{
public:
	void Init();
	void Destroy();

	void SetCurrentScene(Vulture::Scene** scene, Vulture::AssetHandle sceneHandle);

	void Render();

	Editor() = default;
	~Editor() = default;

private:
	void CreateQuadPipeline();
	void CreateQuadRenderTarget();
	void RescaleQuad();
	void CreateQuadDescriptor();

	void RenderViewportImage();
	void RenderImGui();

	void ImGuiPathTracerSettings();
	void ImGuiRenderingToFileSettings();

	void ImGuiRenderPathTracingViewport();
	void ImGuiShaderSettings();
	void ImGuiInfoHeader(bool resetButton);
	void ImGuiSceneEditor();
	void ImGuiEnvMapSettings();
	void ImGuiPathTracingSettings();
	void ImGuiPostProcessingSettings();
	void ImGuiViewportSettings();
	void ImGuiCameraSettings();
	void ImGuiFileRenderSettings();
	void ImGuiSerializationSettings();

	void Resize();

	PathTracer m_PathTracer;
	PostProcessor m_PostProcessor;
	Vulture::Denoiser m_Denoiser;
	Vulture::Image m_DenoisedImage;
	VkDescriptorSet m_PathTracerOutputImageSet = VK_NULL_HANDLE;
	VkDescriptorSet m_DenoisedOutputImageSet = VK_NULL_HANDLE;

	// Scene
	Vulture::Scene** m_CurrentScene = nullptr; // ** because we have to change the pointer inside the Application, so we need a pointer to pointer
	Vulture::AssetHandle m_SceneHandle;
	// Model
	bool m_ModelChanged = false;
	std::string m_ChangedModelFilepath = "";
	void UpdateModel();
	uint64_t m_VertexCount = 0;
	uint64_t m_IndexCount = 0;
	// Skybox
	bool m_SkyboxChanged = false;
	std::string m_ChangedSkyboxFilepath = "";
	void UpdateSkybox();

	Vulture::Timer m_Timer;
	float m_Time = 0;
	bool m_ImGuiViewportResized = false;
	bool m_ImageResized = false;
	VkExtent2D m_ViewportSize = {900, 900};

	bool m_RenderToFile = false;
	bool m_PathTracingFinished = false;
	bool m_ReadyToSaveRender = false;
	bool m_FileAlreadySaved = false;
	bool m_ImageDenoised = false;
	bool m_DenoisedImageReady = false;
	bool m_ShowDenoisedImage = false;

	// Viewport Rendering
	Vulture::OrthographicCamera m_QuadCamera;
	Vulture::Pipeline m_QuadPipeline;
	Vulture::PushConstant<glm::mat4> m_QuadPush;
	Vulture::Transform m_ImageQuadTransform;
	Vulture::Framebuffer m_QuadRenderTarget;
	Vulture::DescriptorSet m_QuadDescriptor;

	bool m_PathTracerViewportVisible = true;
};