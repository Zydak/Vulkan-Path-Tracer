#pragma once
#include "pch.h"
#include <VulkanHelper.h>
#include "Editor.h"

class Application : public VulkanHelper::Application
{
public:
	Application(VulkanHelper::ApplicationInfo appInfo);
	~Application();

	void Destroy() override;

	void OnUpdate(double deltaTime) override;
	void InitScripts();
	void UpdateScripts(double deltaTime);
	void DestroyScripts();
private:
	void Init();

	VulkanHelper::Scene* m_Scene;
	VulkanHelper::Scope<Editor> m_Editor;
};