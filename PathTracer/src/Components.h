#pragma once

#include "VulkanHelper.h"

class PerspectiveCameraComponent
{
public:
	VulkanHelper::PerspectiveCamera Camera{};
	bool MainCamera = false;

	PerspectiveCameraComponent() = default;

	PerspectiveCameraComponent(PerspectiveCameraComponent&& other) noexcept
	{
		Camera = std::move(other.Camera);
		MainCamera = std::move(other.MainCamera);
	}

	std::vector<char> Serialize()
	{
		return VulkanHelper::Bytes::ToBytes(this, sizeof(PerspectiveCameraComponent));
	}

	void Deserialize(const std::vector<char>& bytes)
	{
		PerspectiveCameraComponent comp = VulkanHelper::Bytes::FromBytes<PerspectiveCameraComponent>(bytes);
		memcpy(&Camera, &comp.Camera, sizeof(VulkanHelper::PerspectiveCamera));
		memcpy(&MainCamera, &comp.MainCamera, sizeof(bool));
	}

	static PerspectiveCameraComponent* GetMainCamera(VulkanHelper::Scene* scene)
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

	static VulkanHelper::Entity GetMainCameraEntity(VulkanHelper::Scene* scene)
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
	VulkanHelper::OrthographicCamera Camera{};
	bool MainCamera = false;

	OrthographicCameraComponent() = default;

	OrthographicCameraComponent(OrthographicCameraComponent&& other) noexcept
	{
		Camera = std::move(other.Camera);
		MainCamera = std::move(other.MainCamera);
	}

	std::vector<char> Serialize()
	{
		return VulkanHelper::Bytes::ToBytes(this, sizeof(OrthographicCameraComponent));
	}

	void Deserialize(const std::vector<char>& bytes)
	{
		OrthographicCameraComponent comp = VulkanHelper::Bytes::FromBytes<OrthographicCameraComponent>(bytes);
		memcpy(&Camera, &comp.Camera, sizeof(VulkanHelper::OrthographicCamera));
		memcpy(&MainCamera, &comp.MainCamera, sizeof(bool));
	}

	static OrthographicCameraComponent* GetMainCamera(VulkanHelper::Scene* scene)
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

	static VulkanHelper::Entity GetMainCameraEntity(VulkanHelper::Scene* scene)
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

class SkyboxComponent 
{
public:
	SkyboxComponent() = default;
	SkyboxComponent(const std::string& filepath)
	{
		ImageHandle = VulkanHelper::AssetManager::LoadAsset(filepath);
	}

	SkyboxComponent(SkyboxComponent&& other) noexcept
	{
		ImageHandle = std::move(other.ImageHandle);
	}

	std::vector<char> Serialize()
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

	void Deserialize(const std::vector<char>& bytes)
	{
		std::string path;

		for (int i = 0; i < bytes.size() - 1; i++)
		{
			path.push_back(bytes[i]);
		}

		ImageHandle = VulkanHelper::AssetManager::LoadAsset(path);
	}

	VulkanHelper::AssetHandle ImageHandle;
};

class PathTracingSettingsComponent 
{
public:
	PathTracingSettingsComponent() = default;
	~PathTracingSettingsComponent() = default;
	PathTracingSettingsComponent(PathTracingSettingsComponent&& other) noexcept { Settings = std::move(other.Settings); };
	PathTracingSettingsComponent(const PathTracingSettingsComponent& other) { Settings = other.Settings; };
	PathTracingSettingsComponent& operator=(const PathTracingSettingsComponent& other) { Settings = other.Settings; return *this; };
	PathTracingSettingsComponent& operator=(PathTracingSettingsComponent&& other) noexcept { Settings = std::move(other.Settings); return *this; };

	std::vector<char> Serialize()
	{
		std::vector<char> vec;

		vec.resize(sizeof(PathTracer::DrawInfo));

		memcpy(vec.data(), &Settings, sizeof(PathTracer::DrawInfo));

		return vec;
	}

	void Deserialize(const std::vector<char>& bytes)
	{
		memcpy(&Settings, bytes.data(), sizeof(PathTracer::DrawInfo));
		int currentPos = sizeof(PathTracer::DrawInfo);
	}

	PathTracer::DrawInfo Settings{};
};

class EditorSettingsComponent 
{
public:
	EditorSettingsComponent() = default;
	~EditorSettingsComponent() = default;
	EditorSettingsComponent(EditorSettingsComponent&& other) noexcept { ImageSize = std::move(other.ImageSize); };
	EditorSettingsComponent(const EditorSettingsComponent& other) { ImageSize = other.ImageSize; };
	EditorSettingsComponent& operator=(const EditorSettingsComponent& other) { ImageSize = other.ImageSize; return *this; };
	EditorSettingsComponent& operator=(EditorSettingsComponent&& other) noexcept { ImageSize = std::move(other.ImageSize); return *this; };

	std::vector<char> Serialize()
	{
		std::vector<char> bytes;

		bytes.resize(sizeof(VkOffset2D));

		memcpy(bytes.data(), &ImageSize, sizeof(VkOffset2D));

		return bytes;
	}

	void Deserialize(const std::vector<char>& bytes)
	{
		memcpy(&ImageSize, bytes.data(), sizeof(VkOffset2D));
	}

	VkOffset2D ImageSize = { 900, 900 }; // Using VkOffset2D so that components are ints and not uints for ImGui
};

class AABB
{
public:
	bool IsInside(glm::vec3 point)
	{
		if (
			(A.x <= point.x && B.x >= point.x) &&
			(A.y <= point.y && B.y >= point.y) &&
			(A.z <= point.z && B.z >= point.z)
			)
		{
			return true;
		}
		return false;
	}

	glm::vec3 A;
	glm::vec3 B;
};

class VolumeComponent 
{
public:
	VolumeComponent() = default;
	~VolumeComponent() = default;

	VolumeComponent(VolumeComponent&& other) noexcept 
	{ 
		AABB = std::move(other.AABB); 
		ScatteringCoefficient = std::move(other.ScatteringCoefficient);
		Color = std::move(other.Color);
		G = std::move(other.G);
	};

	VolumeComponent(const VolumeComponent& other) 
	{ 
		AABB = other.AABB; 
		ScatteringCoefficient = other.ScatteringCoefficient;
		Color = other.Color;
		G = other.G;
	};

	VolumeComponent& operator=(const VolumeComponent& other) 
	{ 
		AABB = other.AABB;
		ScatteringCoefficient = other.ScatteringCoefficient;
		Color = other.Color;
		G = other.G;
		return *this; 
	};

	VolumeComponent& operator=(VolumeComponent&& other) noexcept 
	{ 
		AABB = std::move(other.AABB);
		ScatteringCoefficient = std::move(other.ScatteringCoefficient);
		Color = std::move(other.Color);
		G = std::move(other.G);
		return *this; 
	};

	std::vector<char> Serialize()
	{
		std::vector<char> bytes;

		bytes.resize(sizeof(VolumeComponent));

		memcpy(bytes.data(), this, sizeof(VolumeComponent));

		return bytes;
	}

	void Deserialize(const std::vector<char>& bytes)
	{
		memcpy(this, bytes.data(), sizeof(VolumeComponent));
	}
	
	AABB AABB = { glm::vec3{-1.0f}, glm::vec3{1.0f} };
	glm::vec3 Color = glm::vec3(1.0f);
	float ScatteringCoefficient = 10.0f;
	float G = 0.0f;
};