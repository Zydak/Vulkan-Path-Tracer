// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#pragma once
#include "pch.h"
#include <Vulture.h>
#include "Editor.h"

class Application : public Vulture::Application
{
public:
	Application(Vulture::ApplicationInfo appInfo);
	~Application();

	void Destroy() override;

	void OnUpdate(double deltaTime) override;
	void InitScripts();
	void UpdateScripts(double deltaTime);
	void DestroyScripts();
private:
	void Init();

	Vulture::AssetHandle m_Scene;
	Vulture::Scope<Editor> m_Editor;
};