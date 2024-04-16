#pragma once

#include "pch.h"

#include "SceneRenderer.h"

class Vulture::Scene;

class Editor
{
public:
	void Init(Vulture::AssetManager* assetManager);
	void Destroy();

	std::function<void()> GetRenderFunction() { return [this]() { RenderImGui(); }; }; // pozdro

	void SetCurrentScene(Vulture::Scene* scene);

	void Render();

	Editor();
	~Editor();

	SceneRenderer* GetSceneRenderer() { return &(*m_SceneRenderer); };

private:
	void RenderImGui();

	void ImGuiRenderViewport();
	void ImGuiInfoHeader();

	void ImGuiSceneSettings();
	void ImGuiCameraSettings();
	void ImGuiEnvironmentMapSettings();
	void ImGuiPostProcessingSettings();
	void ImGuiPathTracingSettings();
	void ImGuiFileSettings();
	void ImGuiDenoiserSettings();
	void ImGuiShowGBuffer();

	Vulture::AssetManager* m_AssetManager;
	Vulture::Scope<SceneRenderer> m_SceneRenderer;

	Vulture::Scene* m_CurrentScene;

	Vulture::Timer m_Timer;
	Vulture::Timer m_TotalTimer;
	float m_Time = 0;
	bool m_ImGuiViewportResized = false;

	bool m_RecreateRendererResources = false;

	struct DrawFileInfo
	{
		int Resolution[2] = { 1920, 1080 };

		bool Denoised = false;
		bool SaveToFile = false;
		bool ShowDenoised = false;
	};
	DrawFileInfo m_DrawFileInfo{};

	std::vector<Vulture::Material>* m_CurrentMaterials;
	std::vector<std::string> m_CurrentMeshesNames;
};