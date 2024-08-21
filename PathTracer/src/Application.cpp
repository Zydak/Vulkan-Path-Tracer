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
	m_Scene->Destroy();
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
	m_Scene->InitScripts();
}

void Application::UpdateScripts(double deltaTime)
{
	m_Scene->UpdateScripts(deltaTime);
}

void Application::DestroyScripts()
{
	m_Scene->DestroyScripts();
}

void Application::Init()
{
	m_Editor = std::make_unique<Editor>();
	m_Editor->Init();

	REGISTER_CLASS_IN_SERIALIZER(PerspectiveCameraComponent);
	REGISTER_CLASS_IN_SERIALIZER(OrthographicCameraComponent);
	REGISTER_CLASS_IN_SERIALIZER(SkyboxComponent);
	REGISTER_CLASS_IN_SERIALIZER(CameraScript);

	Vulture::AssetHandle sceneHandle = Vulture::AssetManager::LoadSceneAsset<
		PerspectiveCameraComponent,
		OrthographicCameraComponent,
		SkyboxComponent,
		CameraScript,
		Vulture::ScriptComponent,
		Vulture::MeshComponent,
		Vulture::MaterialComponent,
		Vulture::NameComponent,
		Vulture::TransformComponent
	>("assets/scenes/CornellBox.ptscene");

	sceneHandle.WaitToLoad();

	m_Scene = sceneHandle.GetScene();

	// Wait for every component to load
	auto view = m_Scene->GetRegistry().view<Vulture::MeshComponent>();
	for (auto& entity : view)
	{
		Vulture::MeshComponent* meshComp = &m_Scene->GetRegistry().get<Vulture::MeshComponent>(entity);
		meshComp->AssetHandle.WaitToLoad();
	}

	auto view1 = m_Scene->GetRegistry().view<Vulture::MaterialComponent>();
	for (auto& entity : view1)
	{
		Vulture::MaterialComponent* matComp = &m_Scene->GetRegistry().get<Vulture::MaterialComponent>(entity);
		Vulture::Material* mat = matComp->AssetHandle.GetMaterial();
		mat->Textures.CreateSet();
	}

	auto view2 = m_Scene->GetRegistry().view<SkyboxComponent>();
	for (auto& entity : view2)
	{
		SkyboxComponent* skyComp = &m_Scene->GetRegistry().get<SkyboxComponent>(entity);
		skyComp->ImageHandle.WaitToLoad();
	}

	// Initialize editor and path tracing stuff
	m_Editor->SetCurrentScene(&m_Scene, sceneHandle);
}
