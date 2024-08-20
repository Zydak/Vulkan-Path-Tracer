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
	m_Scene.Destroy();
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
	m_Scene.InitScripts();
}

void Application::UpdateScripts(double deltaTime)
{
	m_Scene.UpdateScripts(deltaTime);
}

void Application::DestroyScripts()
{
	m_Scene.DestroyScripts();
}

void Application::Init()
{
	m_Editor = std::make_unique<Editor>();
	m_Editor->Init();

	REGISTER_CLASS_IN_SERIALIZER(PerspectiveCameraComponent);
	REGISTER_CLASS_IN_SERIALIZER(OrthographicCameraComponent);
	REGISTER_CLASS_IN_SERIALIZER(SkyboxComponent);
	REGISTER_CLASS_IN_SERIALIZER(CameraScript);

	//m_Scene.Init();
	//
	//// Add camera
	//Vulture::Entity camera = m_Scene.CreateEntity();
	//auto& camComp = camera.AddComponent<PerspectiveCameraComponent>();
	//camComp.MainCamera = true;
	//camComp.Camera.Translation.z = -16.0f;
	//
	//auto& cameraScComponent = camera.AddComponent<Vulture::ScriptComponent>();
	//cameraScComponent.AddScript<CameraScript>();
	//
	//// Load Initial Skybox
	//Vulture::Entity skyboxEntity = m_Scene.CreateEntity();
	//skyboxEntity.AddComponent<SkyboxComponent>("assets/Black.hdr").ImageHandle.WaitToLoad();
	//
	//// Load Initial model
	//Vulture::AssetHandle modelAssetHandle = Vulture::AssetManager::LoadAsset("assets/cornellBox.gltf");
	//modelAssetHandle.WaitToLoad();
	//
	//Vulture::ModelAsset* modelAsset = (Vulture::ModelAsset*)modelAssetHandle.GetAsset();
	//modelAsset->CreateEntities(&m_Scene);
	//
	////Vulture::Entity entity = m_Scene.CreateEntity();
	////entity.AddComponent<ModelComponent>("assets/cornellBox.gltf").ModelHandle.WaitToLoad();
	////entity.AddComponent<Vulture::TransformComponent>(Vulture::Transform(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 180.0f), glm::vec3(0.5f)));
	//
	//Vulture::Serializer::SerializeScene<
	//	PerspectiveCameraComponent,
	//	OrthographicCameraComponent,
	//	SkyboxComponent,
	//	CameraScript,
	//	Vulture::ScriptComponent,
	//	Vulture::MeshComponent,
	//	Vulture::MaterialComponent,
	//	Vulture::NameComponent,
	//	Vulture::TransformComponent
	//>(&m_Scene, "assets/scenes/Cornell.ptscene");
	//
	//m_Scene.Destroy();

	m_Scene.Init();

	Vulture::Serializer::DeserializeScene<
		PerspectiveCameraComponent,
		OrthographicCameraComponent,
		SkyboxComponent,
		CameraScript,
		Vulture::ScriptComponent,
		Vulture::MeshComponent,
		Vulture::MaterialComponent,
		Vulture::NameComponent,
		Vulture::TransformComponent
	>("assets/scenes/Cornell1.ptscene", &m_Scene);

	// Wait for every component to load
	auto view = m_Scene.GetRegistry().view<Vulture::MeshComponent>();
	for (auto& entity : view)
	{
		Vulture::MeshComponent* meshComp = &m_Scene.GetRegistry().get<Vulture::MeshComponent>(entity);
		meshComp->AssetHandle.WaitToLoad();
	}

	auto view1 = m_Scene.GetRegistry().view<Vulture::MaterialComponent>();
	for (auto& entity : view1)
	{
		Vulture::MaterialComponent* matComp = &m_Scene.GetRegistry().get<Vulture::MaterialComponent>(entity);
		Vulture::Material* mat = matComp->AssetHandle.GetMaterial();
		mat->Textures.CreateSet();
	}

	auto view2 = m_Scene.GetRegistry().view<SkyboxComponent>();
	for (auto& entity : view2)
	{
		SkyboxComponent* skyComp = &m_Scene.GetRegistry().get<SkyboxComponent>(entity);
		skyComp->ImageHandle.WaitToLoad();
	}

	// Initialize editor and path tracing stuff
	m_Editor->SetCurrentScene(&m_Scene);
}
