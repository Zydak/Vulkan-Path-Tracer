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

	void SetCurrentScene(Vulture::Scene* scene);

	void Render();

	Editor();
	~Editor();

private:
	void RenderImGui();

	void ImGuiRenderViewport();
	void ImGuiPathTracerSettings();
	void ImGuiShaderSettings();
	void ImGuiInfoHeader();
	void ImGuiSceneEditor();
	void ImGuiEnvMapSettings();
	void ImGuiPathTracingSettings();

	PathTracer m_PathTracer;
	PostProcessor m_PostProcessor;
	VkDescriptorSet m_PathTracerOutputImageSet;

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
	VkExtent2D m_ViewportSize = {900, 900};
};