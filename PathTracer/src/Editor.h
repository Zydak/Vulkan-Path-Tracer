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
	void UpdateNodeImages();

	PathTracer m_PathTracer;
	PostProcessor m_PostProcessor;

	VkDescriptorSet m_PathTracerOutputImageSet;
	Vulture::Scene* m_CurrentScene;

	float m_Time = 0;
	bool m_ImGuiViewportResized = false;
	VkExtent2D m_ViewportSize = {900, 900};
};