#include "pch.h"
#include "Application.h"
#include "CameraScript.h"
#include "Components.h"

Application::Application(Vulture::ApplicationInfo appInfo)
	: Vulture::Application(appInfo)
{
	m_Scene = std::make_unique<Vulture::Scene>(m_Window);
	Init();
	InitScripts();
}

Application::~Application()
{
	
}

void Application::Destroy()
{
	DestroyScripts();
	Vulture::AssetManager::Destroy();
	m_Scene.reset();

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

	// Add camera
	Vulture::Entity camera = m_Scene->CreateEntity();
	camera.AddComponent<PerspectiveCameraComponent>().MainCamera = true;
	auto& cameraScComponent = camera.AddComponent<Vulture::ScriptComponent>();
	cameraScComponent.AddScript<CameraScript>();

	// Load Initial Skybox
	Vulture::Entity skyboxEntity = m_Scene->CreateEntity();
	skyboxEntity.AddComponent<SkyboxComponent>("assets/Black.hdr").ImageHandle.WaitToLoad();

	// Load Initial model
	Vulture::Entity entity = m_Scene->CreateEntity();
	entity.AddComponent<ModelComponent>("assets/cornellBox.gltf").ModelHandle.WaitToLoad();
	entity.AddComponent<TransformComponent>(Vulture::Transform(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 180.0f), glm::vec3(0.5f)));

	// Initialize path tracing stuff
	m_Editor->SetCurrentScene(m_Scene.get());
}
