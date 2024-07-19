#pragma once

#include "pch.h"

#include "PathTracer.h"
#include "PostProcessor.h"
#include "Rasterizer.h"

class Vulture::Scene;

class Editor
{
public:
	void Init();
	void Destroy();

	void SetCurrentScene(Vulture::Scene* scene);

	void Render();

	Editor();
	~Editor();

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
	void ImGuiRenderRasterizerViewport();
	void ImGuiShaderSettings();
	void ImGuiInfoHeader();
	void ImGuiSceneEditor();
	void ImGuiEnvMapSettings();
	void ImGuiPathTracingSettings();
	void ImGuiViewportSettings();
	void ImGuiCameraSettings();
	void ImGuiFileRenderSettings();

	void Resize();

	Rasterizer m_Rasterizer;
	PathTracer m_PathTracer;
	PostProcessor m_PostProcessor;
	VkDescriptorSet m_PathTracerOutputImageSet;
	VkDescriptorSet m_RasterizerOutputImageSet;

	// Scene
	Vulture::Scene* m_CurrentScene;
	// Model
	bool m_ModelChanged = false;
	std::string m_ChangedModelFilepath = "";
	void UpdateModel();
	// Skybox
	bool m_SkyboxChanged = false;
	std::string m_ChangedSkyboxFilepath = "";
	void UpdateSkybox();

	Vulture::Timer m_Timer;
	float m_Time = 0;
	bool m_ImGuiViewportResized = false;
	bool m_ImageResized = false;
	VkOffset2D m_ImageSize = {900, 900}; // Using offset so that components are ints and not uints for ImGui
	VkExtent2D m_ViewportSize = {900, 900};

	bool m_RenderToFile = false;
	bool m_PathTracingFinished = false;
	bool m_ReadyToSaveRender = false;
	bool m_FileAlreadySaved = false;

	// Viewport Rendering
	Vulture::OrthographicCamera m_QuadCamera;
	Vulture::Pipeline m_QuadPipeline;
	Vulture::PushConstant<glm::mat4> m_QuadPush;
	Vulture::Transform m_ImageQuadTransform;
	Vulture::Framebuffer m_QuadRenderTarget;
	Vulture::DescriptorSet m_QuadDescriptor;

	bool m_PathTracerViewportVisible = true;
	bool m_RasterizerViewportVisible = true;
};