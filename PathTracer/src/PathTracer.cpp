#include "pch.h"
#include "PathTracer.h"
#include "CameraScript.h"

PathTracer::PathTracer(Vulture::ApplicationInfo appInfo, float width, float height)
	: Vulture::Application(appInfo, width, height), m_AssetManager({4})
{
	m_Scene = std::make_unique<Vulture::Scene>(m_Window);
	Init();
	InitScripts();

	{
		Vulture::Timer timer;
		Vulture::AssetHandle handle1(m_AssetManager.LoadAsset("assets/dragon.obj"));
		Vulture::AssetHandle handle2(m_AssetManager.LoadAsset("assets/Dog.gltf"));
		Vulture::AssetHandle handle3(m_AssetManager.LoadAsset("assets/ship.gltf"));
		Vulture::AssetHandle handle4(m_AssetManager.LoadAsset("assets/monster.gltf"));

		//handle1.WaitToLoad();
		//handle2.WaitToLoad();
		//handle3.WaitToLoad();
		//handle4.WaitToLoad();

		VL_ERROR("LOAD TIME: {}s", timer.ElapsedSeconds());
	}
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
	m_Editor->Init();

	// Add camera
	Vulture::Entity camera = m_Scene->CreateCamera();
	auto& cameraScComponent = camera.AddComponent<Vulture::ScriptComponent>();
	cameraScComponent.AddScript<CameraScript>();

	// Load Initial Skybox
	Vulture::Entity skybox = m_Scene->CreateSkybox("assets/sunrise.hdr");
	m_Editor->GetSceneRenderer()->SetSkybox(skybox); // tell renderer which skybox to use

	// Load Initial model
	m_Scene->CreateModel("assets/cornellBox.gltf", Vulture::Transform(glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.5f)));
	
	// Initialize path tracing stuff
	m_Scene->InitAccelerationStructure();
	m_Editor->SetCurrentScene(&(*m_Scene));
}
