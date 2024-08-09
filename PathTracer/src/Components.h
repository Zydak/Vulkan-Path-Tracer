// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#pragma once

#include "Vulture.h"

class PerspectiveCameraComponent
{
public:
	Vulture::PerspectiveCamera Camera{};
	bool MainCamera = false;

	static PerspectiveCameraComponent* GetMainCamera(Vulture::Scene* scene)
	{
		auto cameraView = scene->GetRegistry().view<PerspectiveCameraComponent>();
		PerspectiveCameraComponent* camComp = nullptr;
		for (auto& entity : cameraView)
		{
			PerspectiveCameraComponent& comp = scene->GetRegistry().get<PerspectiveCameraComponent>(entity);
			if (comp.MainCamera)
			{
				camComp = &comp;
				break;
			}
		}

		return camComp;
	}

	static Vulture::Entity GetMainCameraEntity(Vulture::Scene* scene)
	{
		auto cameraView = scene->GetRegistry().view<PerspectiveCameraComponent>();
		for (auto& entity : cameraView)
		{
			PerspectiveCameraComponent& comp = scene->GetRegistry().get<PerspectiveCameraComponent>(entity);
			if (comp.MainCamera)
			{
				return { entity, scene };
			}
		}

		VL_CORE_ASSERT(false, "There is no main camera!");
		return { (entt::entity)0, scene };
	}
};

class OrthographicCameraComponent
{
public:
	Vulture::OrthographicCamera Camera{};
	bool MainCamera = false;

	static OrthographicCameraComponent* GetMainCamera(Vulture::Scene* scene)
	{
		auto cameraView = scene->GetRegistry().view<OrthographicCameraComponent>();
		OrthographicCameraComponent* camComp = nullptr;
		for (auto& entity : cameraView)
		{
			OrthographicCameraComponent& comp = scene->GetRegistry().get<OrthographicCameraComponent>(entity);
			if (comp.MainCamera)
			{
				camComp = &comp;
				break;
			}
		}

		return camComp;
	}

	static Vulture::Entity GetMainCameraEntity(Vulture::Scene* scene)
	{
		auto cameraView = scene->GetRegistry().view<OrthographicCameraComponent>();
		for (auto& entity : cameraView)
		{
			OrthographicCameraComponent& comp = scene->GetRegistry().get<OrthographicCameraComponent>(entity);
			if (comp.MainCamera)
			{
				return { entity, scene };
			}
		}

		VL_CORE_ASSERT(false, "There is no main camera!");
		return { (entt::entity)0, scene };
	}
};


class ModelComponent
{
public:
	ModelComponent(const std::string& filepath)
		: ModelHandle(Vulture::AssetManager::LoadAsset(filepath))
	{

	}

	Vulture::AssetHandle ModelHandle;
};

class TransformComponent
{
public:
	TransformComponent(const Vulture::Transform& transform)
		: Transform(transform)
	{

	}

	TransformComponent(Vulture::Transform&& transform) noexcept
	{
		Transform = std::move(transform);
	}

	Vulture::Transform Transform;
};

class SkyboxComponent
{
public:
	SkyboxComponent(const std::string& filepath)
	{
		ImageHandle = Vulture::AssetManager::LoadAsset(filepath);
	}

	Vulture::AssetHandle ImageHandle;
};