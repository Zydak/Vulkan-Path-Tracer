// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "pch.h"
#include "Application.h"
#include "CameraScript.h"
#include "Components.h"

Application::Application(Vulture::ApplicationInfo appInfo)
	: Vulture::Application(appInfo)
{
	Init();
	InitScripts();
}

Application::~Application()
{
	
}

void Application::Destroy()
{
	DestroyScripts();
	m_Scene.Unload();
	Vulture::AssetManager::Destroy();

	m_Editor.reset();
}

void Application::OnUpdate(double deltaTime)
{
	UpdateScripts(deltaTime);

	m_Editor->Render();
}

void Application::InitScripts()
{
	Vulture::Scene* scene = m_Scene.GetScene();
	scene->InitScripts();
}

void Application::UpdateScripts(double deltaTime)
{
	Vulture::Scene* scene = m_Scene.GetScene();
	scene->UpdateScripts(deltaTime);
}

void Application::DestroyScripts()
{
	Vulture::Scene* scene = m_Scene.GetScene();
	scene->DestroyScripts();
}

void Application::Init()
{
	m_Editor = std::make_unique<Editor>();
	m_Editor->Init();

	REGISTER_CLASS_IN_SERIALIZER(PerspectiveCameraComponent);
	REGISTER_CLASS_IN_SERIALIZER(OrthographicCameraComponent);
	REGISTER_CLASS_IN_SERIALIZER(ModelComponent);
	REGISTER_CLASS_IN_SERIALIZER(TransformComponent);
	REGISTER_CLASS_IN_SERIALIZER(SkyboxComponent);
	REGISTER_CLASS_IN_SERIALIZER(CameraScript);

	m_Scene = Vulture::AssetManager::LoadSceneAsset<ModelComponent, TransformComponent, SkyboxComponent, PerspectiveCameraComponent, OrthographicCameraComponent, Vulture::ScriptComponent>("assets/scenes/CornellBox.ptscene");
	m_Scene.WaitToLoad();
	Vulture::Scene* scene = m_Scene.GetScene();

	auto view = scene->GetRegistry().view<SkyboxComponent>();
	for (auto& entity : view)
	{
		SkyboxComponent* skybox = &scene->GetRegistry().get<SkyboxComponent>(entity);
		skybox->ImageHandle.WaitToLoad();
	}

	auto view1 = scene->GetRegistry().view<ModelComponent>();
	for (auto& entity : view1)
	{
		ModelComponent* model = &scene->GetRegistry().get<ModelComponent>(entity);
		model->ModelHandle.WaitToLoad();
	}

	auto view2 = scene->GetRegistry().view<PerspectiveCameraComponent>();
	for (auto& entity : view2)
	{
		PerspectiveCameraComponent* camera = &scene->GetRegistry().get<PerspectiveCameraComponent>(entity);

		camera->Camera.SetPerspectiveMatrix(45.0f, m_Window->GetAspectRatio(), 0.1f, 100.0f);
	}

	// Initialize path tracing stuff
	m_Editor->SetCurrentScene(scene);
}
