// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#pragma once

#include "Vulture.h"

class PerspectiveCameraComponent : public Vulture::SerializeBaseClass
{
public:
	Vulture::PerspectiveCamera Camera{};
	bool MainCamera = false;

	PerspectiveCameraComponent() = default;

	PerspectiveCameraComponent(PerspectiveCameraComponent&& other) noexcept
	{
		Camera = std::move(other.Camera);
		MainCamera = std::move(other.MainCamera);
	}

	std::vector<char> Serialize() override
	{
		return Vulture::Bytes::ToBytes(this, sizeof(PerspectiveCameraComponent));
	}

	void Deserialize(const std::vector<char>& bytes) override
	{
		PerspectiveCameraComponent comp = Vulture::Bytes::FromBytes<PerspectiveCameraComponent>(bytes);
		memcpy(&Camera, &comp.Camera, sizeof(Vulture::PerspectiveCamera));
		memcpy(&MainCamera, &comp.MainCamera, sizeof(bool));
	}

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

class OrthographicCameraComponent : public Vulture::SerializeBaseClass
{
public:
	Vulture::OrthographicCamera Camera{};
	bool MainCamera = false;

	OrthographicCameraComponent() = default;

	OrthographicCameraComponent(OrthographicCameraComponent&& other) noexcept
	{
		Camera = std::move(other.Camera);
		MainCamera = std::move(other.MainCamera);
	}

	std::vector<char> Serialize() override
	{
		return Vulture::Bytes::ToBytes(this, sizeof(OrthographicCameraComponent));
	}

	void Deserialize(const std::vector<char>& bytes) override
	{
		OrthographicCameraComponent comp = Vulture::Bytes::FromBytes<OrthographicCameraComponent>(bytes);
		memcpy(&Camera, &comp.Camera, sizeof(Vulture::OrthographicCamera));
		memcpy(&MainCamera, &comp.MainCamera, sizeof(bool));
	}

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

class ModelComponent : public virtual Vulture::SerializeBaseClass
{
public:
	ModelComponent() = default;

	ModelComponent(const std::string& filepath)
		: ModelHandle(Vulture::AssetManager::LoadAsset(filepath))
	{

	}

	ModelComponent(ModelComponent&& other) noexcept
	{
		ModelHandle = std::move(other.ModelHandle);
	}

	std::vector<char> Serialize() override
	{
		std::vector<char> vec;
		std::string path = ModelHandle.GetAsset()->GetPath();

		for (int i = 0; i < path.size(); i++)
		{
			vec.push_back(path[i]);
		}
		vec.push_back('\0');

		return vec;
	}

	void Deserialize(const std::vector<char>& bytes) override
	{
		std::string path;

		for (int i = 0; i < bytes.size() - 1; i++)
		{
			path.push_back(bytes[i]);
		}

		ModelHandle = Vulture::AssetManager::LoadAsset(path);
	}

	Vulture::AssetHandle ModelHandle;
};

class TransformComponent : public virtual Vulture::SerializeBaseClass
{
public:
	TransformComponent() = default;
	TransformComponent(const Vulture::Transform& transform)
		: Transform(transform)
	{

	}

	TransformComponent(Vulture::Transform&& transform) noexcept
	{
		Transform = std::move(transform);
	}

	TransformComponent(TransformComponent&& other) noexcept
	{
		Transform = std::move(other.Transform);
	}

	std::vector<char> Serialize() override
	{
		return Vulture::Bytes::ToBytes(this, sizeof(TransformComponent));
	}

	void Deserialize(const std::vector<char>& bytes) override
	{
		TransformComponent comp = Vulture::Bytes::FromBytes<TransformComponent>(bytes);
		memcpy(&Transform, &comp.Transform, sizeof(Vulture::Transform));
	}

	Vulture::Transform Transform;
};

class SkyboxComponent : public virtual Vulture::SerializeBaseClass
{
public:
	SkyboxComponent() = default;
	SkyboxComponent(const std::string& filepath)
	{
		ImageHandle = Vulture::AssetManager::LoadAsset(filepath);
	}

	SkyboxComponent(SkyboxComponent&& other) noexcept
	{
		ImageHandle = std::move(other.ImageHandle);
	}

	std::vector<char> Serialize() override
	{
		std::vector<char> vec;
		std::string path = ImageHandle.GetAsset()->GetPath();

		for (int i = 0; i < path.size(); i++)
		{
			vec.push_back(path[i]);
		}
		vec.push_back('\0');

		return vec;
	}

	void Deserialize(const std::vector<char>& bytes) override
	{
		std::string path;

		for (int i = 0; i < bytes.size() - 1; i++)
		{
			path.push_back(bytes[i]);
		}

		ImageHandle = Vulture::AssetManager::LoadAsset(path);
	}

	Vulture::AssetHandle ImageHandle;
};