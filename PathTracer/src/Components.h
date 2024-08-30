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

class PathTracingSettingsComponent : public virtual Vulture::SerializeBaseClass
{
public:
	PathTracingSettingsComponent() = default;
	~PathTracingSettingsComponent() = default;
	PathTracingSettingsComponent(PathTracingSettingsComponent&& other) noexcept { Settings = std::move(other.Settings); };
	PathTracingSettingsComponent(const PathTracingSettingsComponent& other) { Settings = other.Settings; };
	PathTracingSettingsComponent& operator=(const PathTracingSettingsComponent& other) { Settings = other.Settings; return *this; };
	PathTracingSettingsComponent& operator=(PathTracingSettingsComponent&& other) noexcept { Settings = std::move(other.Settings); return *this; };

	std::vector<char> Serialize() override
	{
		std::vector<char> vec;

		vec.resize(sizeof(PathTracer::DrawInfo) - sizeof(std::string) * 3);

		memcpy(vec.data(), &Settings, sizeof(PathTracer::DrawInfo) - sizeof(std::string) * 3);

		for (int i = 0; i < Settings.HitShaderPath.size(); i++)
		{
			vec.push_back(Settings.HitShaderPath[i]);
		}
		vec.push_back('\0');

		for (int i = 0; i < Settings.MissShaderPath.size(); i++)
		{
			vec.push_back(Settings.MissShaderPath[i]);
		}
		vec.push_back('\0');

		for (int i = 0; i < Settings.RayGenShaderPath.size(); i++)
		{
			vec.push_back(Settings.RayGenShaderPath[i]);
		}
		vec.push_back('\0');

		return vec;
	}

	void Deserialize(const std::vector<char>& bytes) override
	{
		memcpy(&Settings, bytes.data(), sizeof(PathTracer::DrawInfo) - sizeof(std::string) * 3);
		int currentPos = sizeof(PathTracer::DrawInfo) - sizeof(std::string) * 3;

		std::string hitPath;
		while (true)
		{
			char ch = bytes[currentPos];
			currentPos++;

			if (ch == '\0')
				break;
			else
				hitPath.push_back(ch);
		}

		std::string missPath;
		while (true)
		{
			char ch = bytes[currentPos];
			currentPos++;

			if (ch == '\0')
				break;
			else
				missPath.push_back(ch);
		}

		std::string rayGenPath;
		while (true)
		{
			char ch = bytes[currentPos];
			currentPos++;

			if (ch == '\0')
				break;
			else
				rayGenPath.push_back(ch);
		}

		Settings.HitShaderPath		= std::move(hitPath);
		Settings.MissShaderPath		= std::move(missPath);
		Settings.RayGenShaderPath	= std::move(rayGenPath);
	}

	PathTracer::DrawInfo Settings{};
};

class EditorSettingsComponent : public virtual Vulture::SerializeBaseClass
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