#include "pch.h"
#include "PathTracer.h"
#include "CameraScript.h"

PathTracer::PathTracer(Vulture::ApplicationInfo appInfo, float width, float height)
	: Vulture::Application(appInfo, width, height), m_AssetManager({4}) // TODO: check for amount of cores
{
	m_Scene = std::make_unique<Vulture::Scene>(m_Window, &m_AssetManager);
	Init();
	InitScripts();
}

PathTracer::~PathTracer()
{
	
}

void PathTracer::Destroy()
{
	DestroyScripts();
	m_AssetManager.Destroy();
	m_Scene.reset();

	m_Editor.reset();
}

void PathTracer::OnUpdate(double deltaTime)
{
	UpdateScripts(deltaTime);

	m_Editor->Render();
}

void PathTracer::InitScripts()
{
	m_Scene->InitScripts();
}

void PathTracer::UpdateScripts(double deltaTime)
{
	m_Scene->UpdateScripts(deltaTime);
}

void PathTracer::DestroyScripts()
{
	m_Scene->DestroyScripts();
}

void PathTracer::Init()
{
	m_Editor = std::make_unique<Editor>();
	m_Editor->Init(&m_AssetManager);

	// Add camera
	Vulture::Entity camera = m_Scene->CreateCamera();
	auto& cameraScComponent = camera.AddComponent<Vulture::ScriptComponent>();
	cameraScComponent.AddScript<CameraScript>();

	// Load Initial Skybox
	Vulture::Entity skybox = m_Scene->CreateSkybox("assets/sunrise.hdr");
	m_Editor->GetSceneRenderer()->SetSkybox(skybox); // tell renderer which skybox to use

	// Load Initial model
	Vulture::Entity entity = m_Scene->CreateModel("assets/cornellBox.gltf", Vulture::Transform(glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.5f)));
	entity.GetComponent<Vulture::ModelComponent>().ModelHandle.WaitToLoad();

	// Initialize path tracing stuff
	m_Scene->InitAccelerationStructure();
	m_Editor->SetCurrentScene(&(*m_Scene));
}
