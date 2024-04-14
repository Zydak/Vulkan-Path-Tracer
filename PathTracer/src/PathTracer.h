#pragma once
#include "pch.h"
#include <Vulture.h>
#include "Editor.h"

class PathTracer : public Vulture::Application
{
public:
	PathTracer(Vulture::ApplicationInfo appInfo, float width, float height);
	~PathTracer();

	void Destroy() override;

	void OnUpdate(double deltaTime) override;
	void InitScripts();
	void UpdateScripts(double deltaTime);
	void DestroyScripts();
private:
	void Init();

	Vulture::Scope<Vulture::Scene> m_Scene;
	Vulture::Scope<Editor> m_Editor;
	Vulture::AssetManager m_AssetManager;
};