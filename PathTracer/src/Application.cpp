#include "pch.h"
#include "Application.h"
#include "CameraScript.h"
#include "Components.h"

#include "glm/glm/gtx/matrix_decompose.hpp"

Application::Application(VulkanHelper::ApplicationInfo appInfo)
	: VulkanHelper::Application(appInfo)
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
	VulkanHelper::AssetManager::Destroy();

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

	m_Scene = new VulkanHelper::Scene();
	m_Scene->Init();

	// Add camera
	VulkanHelper::Entity camera = m_Scene->CreateEntity();
	auto& camComp = camera.AddComponent<PerspectiveCameraComponent>();
	camComp.MainCamera = true;
	camComp.Camera.Translation.z = -16.0f;

	auto& cameraScComponent = camera.AddComponent<VulkanHelper::ScriptComponent>();
	cameraScComponent.AddScript<CameraScript>();

	// Load Initial Skybox
	VulkanHelper::Entity skyboxEntity = m_Scene->CreateEntity();
	skyboxEntity.AddComponent<SkyboxComponent>("assets/Black.hdr").ImageHandle.WaitToLoad();

	// Load Initial model
	VulkanHelper::AssetHandle modelAssetHandle = VulkanHelper::AssetManager::LoadAsset("assets/cornellBox.gltf");
	modelAssetHandle.WaitToLoad();

	VulkanHelper::ModelAsset* modelAsset = (VulkanHelper::ModelAsset*)modelAssetHandle.GetAsset();
	modelAsset->CreateEntities(m_Scene);

	modelAssetHandle.Unload();

	VulkanHelper::Serializer::SerializeScene<
		PerspectiveCameraComponent,
		OrthographicCameraComponent,
		SkyboxComponent,
		VulkanHelper::ScriptComponent,
		VulkanHelper::MeshComponent,
		VulkanHelper::MaterialComponent,
		VulkanHelper::NameComponent,
		VulkanHelper::TransformComponent
	>(m_Scene, "assets/scenes/CornellBox.ptscene");

	// Unload everything
	{
		auto view = m_Scene->GetRegistry().view<VulkanHelper::MeshComponent, VulkanHelper::MaterialComponent>();
		for (auto& entity : view)
		{
			auto [meshComp, materialComp] = m_Scene->GetRegistry().get<VulkanHelper::MeshComponent, VulkanHelper::MaterialComponent>(entity);

			meshComp.AssetHandle.Unload();

			if (materialComp.AssetHandle.DoesHandleExist())
				materialComp.AssetHandle.Unload();
		}
	}

	m_Scene->Destroy();

	delete m_Scene;

	REGISTER_CLASS_IN_SERIALIZER(PerspectiveCameraComponent);
	REGISTER_CLASS_IN_SERIALIZER(OrthographicCameraComponent);
	REGISTER_CLASS_IN_SERIALIZER(SkyboxComponent);
	REGISTER_CLASS_IN_SERIALIZER(CameraScript);
	REGISTER_CLASS_IN_SERIALIZER(PathTracingSettingsComponent);
	REGISTER_CLASS_IN_SERIALIZER(EditorSettingsComponent);
	REGISTER_CLASS_IN_SERIALIZER(VolumeComponent);

	VulkanHelper::AssetHandle sceneHandle = VulkanHelper::AssetManager::LoadSceneAsset<
		PerspectiveCameraComponent,
		OrthographicCameraComponent,
		SkyboxComponent,
		PathTracingSettingsComponent,
		EditorSettingsComponent,
		VolumeComponent,
		VulkanHelper::ScriptComponent,
		VulkanHelper::MeshComponent,
		VulkanHelper::MaterialComponent,
		VulkanHelper::NameComponent,
		VulkanHelper::TransformComponent,
		VulkanHelper::TonemapperSettingsComponent,
		VulkanHelper::BloomSettingsComponent
	>("assets/scenes/CornellBox.ptscene");

	sceneHandle.WaitToLoad();

	m_Scene = sceneHandle.GetScene();

	// Wait for every component to load
	auto view = m_Scene->GetRegistry().view<VulkanHelper::MeshComponent>();
	for (auto& entity : view)
	{
		VulkanHelper::MeshComponent* meshComp = &m_Scene->GetRegistry().get<VulkanHelper::MeshComponent>(entity);
		meshComp->AssetHandle.WaitToLoad();
	}

	auto view1 = m_Scene->GetRegistry().view<VulkanHelper::MaterialComponent>();
	for (auto& entity : view1)
	{
		VulkanHelper::MaterialComponent* matComp = &m_Scene->GetRegistry().get<VulkanHelper::MaterialComponent>(entity);
		VulkanHelper::Material* mat = matComp->AssetHandle.GetMaterial();
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
