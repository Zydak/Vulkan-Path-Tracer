#pragma once
#include "pch.h"
#include <VulkanHelper.h>
#include "Editor.h"

class Application
{
public:
	Application(std::shared_ptr<VulkanHelper::Window> window);
	~Application();

	void Destroy();

	void Run();
	void InitScripts();
	void UpdateScripts(double deltaTime);
	void DestroyScripts();
private:
	void Init();

	std::shared_ptr<VulkanHelper::Window> m_Window;
	VulkanHelper::Scene* m_Scene;
	VulkanHelper::Scope<Editor> m_Editor;
};