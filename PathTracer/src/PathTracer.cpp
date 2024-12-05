#include "PathTracer.h"
#include "Components.h"
#include <glm/gtc/random.hpp>

#include "EnergyLossCalculator.h"

void PathTracer::Init(VkExtent2D size)
{
	m_ViewportSize = size;

	CreateFramebuffers();

	CreateDescriptorSets();

	BuildEnergyLookupTable();

	VulkanHelper::Image::CreateInfo info{};
	info.Properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	info.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.Width = 32;
	info.Height = 32;
	info.Format = VK_FORMAT_R32_SFLOAT;
	info.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	info.LayerCount = 32;
	m_ReflectionLookupTexture.Init(info);

	VkCommandBuffer cmd;
	VulkanHelper::Device::BeginSingleTimeCommands(cmd, VulkanHelper::Device::GetGraphicsCommandPool());

	for (int i = 0; i < 32; i++)
	{
		m_ReflectionLookupTexture.WritePixels(m_ReflectionEnergyLookupTable.data() + 32 * 32 * i, cmd, i);
	}

	info.Width = 256;
	info.Height = 64;
	info.LayerCount = 64;
	m_RefractionLookupTextureEtaGreaterThan1.Init(info);
	m_RefractionLookupTextureEtaLessThan1.Init(info);

	for (int i = 0; i < 64; i++)
	{
		m_RefractionLookupTextureEtaGreaterThan1.WritePixels(m_RefractionEtaGreaterThan1EnergyLookupTable.data() + 256 * 64 * i, cmd, i);
	}

	for (int i = 0; i < 64; i++)
	{
		m_RefractionLookupTextureEtaLessThan1.WritePixels(m_RefractionEtaLessThan1EnergyLookupTable.data() + 256 * 64 * i, cmd, i);
	}

	VulkanHelper::Device::EndSingleTimeCommands(cmd, VulkanHelper::Device::GetGraphicsQueue(), VulkanHelper::Device::GetGraphicsCommandPool());

	VulkanHelper::Buffer::CreateInfo bufferInfo{};
	bufferInfo.InstanceSize = 100 * sizeof(VolumeComponent);
	bufferInfo.MemoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	bufferInfo.UsageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	m_VolumesBuffer.Init(bufferInfo);
}

void PathTracer::Resize(VkExtent2D newSize)
{
	m_ViewportSize = newSize;
	VulkanHelper::Device::WaitIdle();
	ResetFrameAccumulation();

	CreateFramebuffers();

	// Update Ray tracing set
	VkDescriptorImageInfo info = { 
		VulkanHelper::Renderer::GetLinearSampler().GetSamplerHandle(), 
		m_PathTracingImage.GetImageView(), 
		VK_IMAGE_LAYOUT_GENERAL 
	};

	m_RayTracingDescriptorSet.UpdateImageSampler(
		1, 
		info
	);

	UpdateDescriptorSetsData();
}

void PathTracer::SetScene(VulkanHelper::Scene* scene)
{
	m_CurrentSceneRendered = scene;

	auto viewPathTracing = m_CurrentSceneRendered->GetRegistry().view<PathTracingSettingsComponent>();
	for (auto& entity : viewPathTracing)
	{
		VL_CORE_ASSERT(m_PathTracingSettings == 0, "Can't have more than one tonemap settings inside a scene!");
		m_PathTracingSettings = VulkanHelper::Entity(entity, m_CurrentSceneRendered);
	}

	// No settings found, create one
	if (m_PathTracingSettings == 0)
	{
		m_PathTracingSettings = m_CurrentSceneRendered->CreateEntity();
		m_PathTracingSettings.AddComponent<PathTracingSettingsComponent>();
	}

	auto view = scene->GetRegistry().view<SkyboxComponent>();
	SkyboxComponent* skybox = nullptr;
	for (auto& entity : view)
	{
		skybox = &scene->GetRegistry().get<SkyboxComponent>(entity);
	}

	VL_CORE_ASSERT(skybox != nullptr, "There is no skybox in the scene!");

	VkDescriptorImageInfo info = { 
		VulkanHelper::Renderer::GetLinearSampler().GetSamplerHandle(),
		skybox->ImageHandle.GetImage()->GetImageView(),
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL 
	};

	m_GlobalDescriptorSets.UpdateImageSampler(
		1,
		info
	);

	m_GlobalDescriptorSets.UpdateBuffer(2, skybox->ImageHandle.GetImage()->GetAccelBuffer()->DescriptorInfo());

	CreateAccelerationStructure();

	CreateRayTracingDescriptorSets();

	UpdateDescriptorSetsData();

	// Write volumes data

	int volumesCount = 0;
	auto volumesView = m_CurrentSceneRendered->GetRegistry().view<VolumeComponent>();
	for (auto& entity : volumesView)
	{
		auto& volumeComp = m_CurrentSceneRendered->GetRegistry().get<VolumeComponent>(entity);
		m_VolumesBuffer.WriteToBuffer(&volumeComp, sizeof(VolumeComponent), volumesCount * sizeof(VolumeComponent));
		volumesCount++;
		
		VL_CHECK(volumesCount < 100, "Can't have more than 100 volumes!");
	}
}

bool PathTracer::Render()
{
	PathTracingSettingsComponent* pathTracingSettings = &m_PathTracingSettings.GetComponent<PathTracingSettingsComponent>();

	VulkanHelper::Device::InsertLabel(VulkanHelper::Renderer::GetCurrentCommandBuffer(), "Inserted label", { 0.0f, 1.0f, 0.0f, 1.0f }); // test

	if (m_PathTracingImage.GetLayout() != VK_IMAGE_LAYOUT_GENERAL)
	{
		m_PathTracingImage.TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL, VulkanHelper::Renderer::GetCurrentCommandBuffer());
	}

	int volumesCount = 0;
	auto view = m_CurrentSceneRendered->GetRegistry().view<VolumeComponent>();
	for (auto& entity : view)
	{
		volumesCount++;
	}

	// Set Push data
	auto data = m_PushContantRayTrace.GetDataPtr();
	data->maxDepth = pathTracingSettings->Settings.RayDepth;
	data->FocalLength = pathTracingSettings->Settings.FocalLength;
	data->DoFStrength = pathTracingSettings->Settings.DOFStrength;
	data->AliasingJitter = pathTracingSettings->Settings.AliasingJitterStr;
	data->SuppressCausticsLuminance = pathTracingSettings->Settings.CausticsSuppresionMaxLuminance;
	data->SamplesPerFrame = pathTracingSettings->Settings.SamplesPerFrame;
	data->EnvAzimuth = glm::radians(pathTracingSettings->Settings.EnvAzimuth);
	data->EnvAltitude = glm::radians(pathTracingSettings->Settings.EnvAltitude);
	data->VolumesCount = volumesCount;

	static glm::mat4 previousViewMat{ 0.0f };
	static glm::mat4 previousProjMat{ 0.0f };
	auto camComp = PerspectiveCameraComponent::GetMainCamera(m_CurrentSceneRendered);
	VL_CORE_ASSERT(camComp != nullptr, "There is no camera");

	if (previousViewMat != camComp->Camera.ViewMat ||
		previousProjMat != camComp->Camera.ProjMat) // if camera moved
	{
		UpdateDescriptorSetsData();
		ResetFrameAccumulation();
		previousViewMat = camComp->Camera.ViewMat;
		previousProjMat = camComp->Camera.ProjMat;
	}
	else
	{
		if (m_CurrentSamplesPerPixel >= (uint32_t)pathTracingSettings->Settings.TotalSamplesPerPixel)
		{
			return false;
		}

		m_CurrentSamplesPerPixel += pathTracingSettings->Settings.SamplesPerFrame;
	}

	m_GBufferNormal.TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL, VulkanHelper::Renderer::GetCurrentCommandBuffer());
	m_GBufferAlbedo.TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL, VulkanHelper::Renderer::GetCurrentCommandBuffer());

	VulkanHelper::Device::BeginLabel(VulkanHelper::Renderer::GetCurrentCommandBuffer(), "Ray Trace Pass", { 1.0f, 0.0f, 0.0f, 1.0f });

	m_RtPipeline.Bind(VulkanHelper::Renderer::GetCurrentCommandBuffer());
	m_RayTracingDescriptorSet.Bind(
		0,
		m_RtPipeline.GetPipelineLayout(),
		VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
		VulkanHelper::Renderer::GetCurrentCommandBuffer()
	);
	m_GlobalDescriptorSets.Bind(
		1,
		m_RtPipeline.GetPipelineLayout(),
		VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
		VulkanHelper::Renderer::GetCurrentCommandBuffer()
	);

	m_PushContantRayTrace.Push(m_RtPipeline.GetPipelineLayout(), VulkanHelper::Renderer::GetCurrentCommandBuffer());

	VulkanHelper::Renderer::RayTrace(VulkanHelper::Renderer::GetCurrentCommandBuffer(), &m_SBT, m_PathTracingImage.GetImageSize());
	m_PushContantRayTrace.GetDataPtr()->frame++;

	VulkanHelper::Device::EndLabel(VulkanHelper::Renderer::GetCurrentCommandBuffer());

	if (pathTracingSettings->Settings.AutoDoF && m_PushContantRayTrace.GetDataPtr()->frame > 1)
	{
		m_RayTracingDoFBuffer.Flush();
		memcpy(&pathTracingSettings->Settings.FocalLength, m_RayTracingDoFBuffer.GetMappedMemory(), sizeof(float));
	}

	VulkanHelper::PerspectiveCamera* cam = &PerspectiveCameraComponent::GetMainCamera(m_CurrentSceneRendered)->Camera;

	m_GBufferNormal.TransitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VulkanHelper::Renderer::GetCurrentCommandBuffer());
	m_GBufferAlbedo.TransitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VulkanHelper::Renderer::GetCurrentCommandBuffer());

	return true;
}

void PathTracer::UpdateResources()
{
	if (m_RecreateRTPipeline)
	{
		CreateRayTracingPipeline();
		m_RecreateRTPipeline = false;
	}
}

void PathTracer::ResetFrameAccumulation()
{
	m_PushContantRayTrace.GetDataPtr()->frame = 0;
	m_CurrentSamplesPerPixel = 0;
}

void PathTracer::RecreateRayTracingPipeline()
{
	m_RecreateRTPipeline = true;
}

void PathTracer::CreateDescriptorSets()
{
	VulkanHelper::Buffer::CreateInfo bufferInfo{};
	bufferInfo.InstanceSize = sizeof(GlobalUbo);
	bufferInfo.MemoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	bufferInfo.UsageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	m_GlobalSetBuffer.Init(bufferInfo);

	VulkanHelper::DescriptorSetLayout::Binding bin{ 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR };
	VulkanHelper::DescriptorSetLayout::Binding bin1{ 1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_RAYGEN_BIT_KHR };
	VulkanHelper::DescriptorSetLayout::Binding bin2{ 2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_RAYGEN_BIT_KHR };

	m_GlobalDescriptorSets.Init(&VulkanHelper::Renderer::GetDescriptorPool(), { bin, bin1, bin2 }, &VulkanHelper::Renderer::GetLinearSampler());
	m_GlobalDescriptorSets.AddBuffer(0, m_GlobalSetBuffer.DescriptorInfo());

	m_GlobalDescriptorSets.Build();
}

void PathTracer::CreateRayTracingPipeline()
{
	PathTracingSettingsComponent* pathTracingSettings = &m_PathTracingSettings.GetComponent<PathTracingSettingsComponent>();

	ResetFrameAccumulation();
	{
		VulkanHelper::PushConstant<PushConstantRay>::CreateInfo pushInfo{};
		pushInfo.Stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;

		m_PushContantRayTrace.Init(pushInfo);

		VulkanHelper::Pipeline::RayTracingCreateInfo info{};
		info.PushConstants = m_PushContantRayTrace.GetRangePtr();

		std::vector<VulkanHelper::Shader::Define> defines;
		defines.reserve(3);
		if (pathTracingSettings->Settings.UseCausticsSuppresion)
			defines.push_back({ "USE_CAUSTICS_SUPPRESION" });
		if (pathTracingSettings->Settings.ShowSkybox)
			defines.push_back({ "SHOW_SKYBOX" });
		if (pathTracingSettings->Settings.FurnaceTestMode)
			defines.push_back({ "FURNACE_TEST_MODE" });

		//src/shaders/raytrace.rgen
		//src/shaders/rgen.slang
		VulkanHelper::Shader rgShader;
		if (!rgShader.Init({ "src/shaders/rgen.slang", VK_SHADER_STAGE_RAYGEN_BIT_KHR, defines }))
			return;

		//src/shaders/raytrace.rchit
		//src/shaders/rchit.slang
		VulkanHelper::Shader htShader;
		if (!htShader.Init({ "src/shaders/rchit.slang", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, defines }))
			return;

		//src/shaders/raytrace.rmiss
		//src/shaders/rtmiss.slang
		VulkanHelper::Shader msShader;
		if (!msShader.Init({ "src/shaders/rtmiss.slang", VK_SHADER_STAGE_MISS_BIT_KHR, defines }))
			return;

		info.RayGenShaders.push_back(&rgShader);
		info.HitShaders.push_back(&htShader);
		info.MissShaders.push_back(&msShader);

		info.DescriptorSetLayouts = {
				m_RayTracingDescriptorSet.GetDescriptorSetLayout()->GetDescriptorSetLayoutHandle(),
				m_GlobalDescriptorSets.GetDescriptorSetLayout()->GetDescriptorSetLayoutHandle()
		};
		info.debugName = "Ray Tracing Pipeline";

		m_RtPipeline.Init(info);
	}
}

void PathTracer::CreateShaderBindingTable()
{
	VulkanHelper::SBT::CreateInfo info{};
	info.CallableCount = 0;
	info.HitCount = 1;
	info.MissCount = 1;
	info.RGenCount = 1;
	info.RayTracingPipeline = &m_RtPipeline;

	m_SBT.Init(&info);
}

void PathTracer::CreateFramebuffers()
{
	// Path Tracing
	{
		VulkanHelper::Image::CreateInfo info{};
		info.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		info.Format = VK_FORMAT_R32G32B32A32_SFLOAT;
		info.Height = m_ViewportSize.height;
		info.Width = m_ViewportSize.width;
		info.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.Properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		info.SamplerInfo = VulkanHelper::SamplerInfo{};
		info.DebugName = "Path Tracing Image";
		m_PathTracingImage.Init(info);
		m_PathTracingImage.TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL);
	}

	// GBuffer
	{
		VulkanHelper::Image::CreateInfo info{};
		info.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		info.Format = VK_FORMAT_R32G32B32A32_SFLOAT;
		info.Height = m_ViewportSize.height;
		info.Width = m_ViewportSize.width;
		info.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.Properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		info.SamplerInfo = VulkanHelper::SamplerInfo{};
		info.DebugName = "GBufferAlbedo";
		m_GBufferAlbedo.Init(info);
		m_GBufferAlbedo.TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL);

		m_GBufferNormal.Init(info);
		m_GBufferNormal.TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL);
	}
}

void PathTracer::CreateRayTracingDescriptorSets()
{
	{
		VulkanHelper::Buffer::CreateInfo bufferInfo{};
		bufferInfo.InstanceSize = sizeof(MeshAdresses) * 50000; // TODO: dynamic amount of meshes
		bufferInfo.MemoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		bufferInfo.UsageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		m_RayTracingMeshesBuffer.Init(bufferInfo);
	}

	{
		VulkanHelper::Buffer::CreateInfo bufferInfo{};
		bufferInfo.InstanceSize = sizeof(VulkanHelper::MaterialProperties) * 50000; // TODO: dynamic amount of meshes
		bufferInfo.MemoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		bufferInfo.UsageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		m_RayTracingMaterialsBuffer.Init(bufferInfo);
	}

	{
		VulkanHelper::Buffer::CreateInfo bufferInfo{};
		bufferInfo.InstanceSize = sizeof(float);
		bufferInfo.MemoryPropertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		bufferInfo.UsageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		m_RayTracingDoFBuffer.Init(bufferInfo);
		m_RayTracingDoFBuffer.Map();
	}

	uint32_t texturesCount = 0;
	{
		auto& reg = m_CurrentSceneRendered->GetRegistry();
		auto view = reg.view<VulkanHelper::MeshComponent>();
		for (auto& entity : view)
		{
			texturesCount += 4; // Each mesh has 4 textures: albedo, normal, rough, metal
		}
	}

	{
		VulkanHelper::DescriptorSetLayout::Binding bin0{ 0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		VulkanHelper::DescriptorSetLayout::Binding bin1{ 1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR };
		VulkanHelper::DescriptorSetLayout::Binding bin2{ 2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		VulkanHelper::DescriptorSetLayout::Binding bin3{ 3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		VulkanHelper::DescriptorSetLayout::Binding bin4{ 4, texturesCount / 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR }; // / 4 because we only care about single texture type like Albedo and not all 4 of them
		VulkanHelper::DescriptorSetLayout::Binding bin5{ 5, texturesCount / 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		VulkanHelper::DescriptorSetLayout::Binding bin6{ 6, texturesCount / 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		VulkanHelper::DescriptorSetLayout::Binding bin7{ 7, texturesCount / 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		VulkanHelper::DescriptorSetLayout::Binding bin8{ 8, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR };
		VulkanHelper::DescriptorSetLayout::Binding bin9{ 9, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		VulkanHelper::DescriptorSetLayout::Binding bin10{ 10, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		VulkanHelper::DescriptorSetLayout::Binding bin11{ 11, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		VulkanHelper::DescriptorSetLayout::Binding bin12{ 12, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		VulkanHelper::DescriptorSetLayout::Binding bin13{ 13, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		VulkanHelper::DescriptorSetLayout::Binding bin14{ 14, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR };
		VulkanHelper::DescriptorSetLayout::Binding bin15{ 15, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR };

		m_RayTracingDescriptorSet.Init(&VulkanHelper::Renderer::GetDescriptorPool(), { bin0, bin1, bin2, bin3, bin4, bin5, bin6, bin7, bin8, bin9, bin10, bin11, bin12, bin13, bin14, bin15 }, &VulkanHelper::Renderer::GetLinearSampler());

		VkAccelerationStructureKHR geometryTlas = m_GeometryAS.GetTlas().Accel;
		VkAccelerationStructureKHR volumeTlas = m_VolumesAS.GetTlas().Accel;

		VkWriteDescriptorSetAccelerationStructureKHR geometryAsInfo{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
		geometryAsInfo.accelerationStructureCount = 1;
		geometryAsInfo.pAccelerationStructures = &geometryTlas;

		VkWriteDescriptorSetAccelerationStructureKHR volumeAsInfo{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
		volumeAsInfo.accelerationStructureCount = 1;
		volumeAsInfo.pAccelerationStructures = &volumeTlas;

		m_RayTracingDescriptorSet.AddAccelerationStructure(0, geometryAsInfo);
		m_RayTracingDescriptorSet.AddAccelerationStructure(13, volumeAsInfo);

		m_RayTracingDescriptorSet.AddImageSampler(1, { VulkanHelper::Renderer::GetLinearSampler().GetSamplerHandle(), m_PathTracingImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL });
		m_RayTracingDescriptorSet.AddBuffer(2, m_RayTracingMeshesBuffer.DescriptorInfo());
		m_RayTracingDescriptorSet.AddBuffer(3, m_RayTracingMaterialsBuffer.DescriptorInfo());

		auto& reg = m_CurrentSceneRendered->GetRegistry();
		auto view = reg.view<VulkanHelper::MeshComponent, VulkanHelper::MaterialComponent>();
		for (auto& entity : view)
		{
			auto [meshComp, materialComp] = reg.get<VulkanHelper::MeshComponent, VulkanHelper::MaterialComponent>(entity);
			VulkanHelper::MaterialTextures* materialTextures = &materialComp.AssetHandle.GetMaterial()->Textures;

			m_RayTracingDescriptorSet.AddImageSampler(
				4,
				{ VulkanHelper::Renderer::GetLinearRepeatSampler().GetSamplerHandle(),
				materialTextures->GetAlbedo().GetImage()->GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
			);

			m_RayTracingDescriptorSet.AddImageSampler(
				5,
				{ VulkanHelper::Renderer::GetLinearRepeatSampler().GetSamplerHandle(),
				materialTextures->GetNormal().GetImage()->GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
			);

			m_RayTracingDescriptorSet.AddImageSampler(
				6,
				{ VulkanHelper::Renderer::GetLinearRepeatSampler().GetSamplerHandle(),
				materialTextures->GetRoughness().GetImage()->GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
			);

			m_RayTracingDescriptorSet.AddImageSampler(
				7,
				{ VulkanHelper::Renderer::GetLinearRepeatSampler().GetSamplerHandle(),
				materialTextures->GetMetallness().GetImage()->GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
			);
		}

		m_RayTracingDescriptorSet.AddBuffer(8, m_RayTracingDoFBuffer.DescriptorInfo());

		m_RayTracingDescriptorSet.AddImageSampler(
			9, 
			{ VulkanHelper::Renderer::GetLinearSampler().GetSamplerHandle(),
				m_ReflectionLookupTexture.GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
		);

		m_RayTracingDescriptorSet.AddImageSampler(
			10,
			{ VulkanHelper::Renderer::GetLinearSampler().GetSamplerHandle(),
				m_RefractionLookupTextureEtaGreaterThan1.GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
		);

		m_RayTracingDescriptorSet.AddImageSampler(
			11,
			{ VulkanHelper::Renderer::GetLinearSampler().GetSamplerHandle(),
				m_RefractionLookupTextureEtaLessThan1.GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
		);

		m_RayTracingDescriptorSet.AddImageSampler(
			14,
			{ VulkanHelper::Renderer::GetLinearSampler().GetSamplerHandle(),
				m_GBufferAlbedo.GetImageView(),
				VK_IMAGE_LAYOUT_GENERAL }
		);

		m_RayTracingDescriptorSet.AddImageSampler(
			15,
			{ VulkanHelper::Renderer::GetLinearSampler().GetSamplerHandle(),
				m_GBufferNormal.GetImageView(),
				VK_IMAGE_LAYOUT_GENERAL }
		);

		m_RayTracingDescriptorSet.AddBuffer(12, m_VolumesBuffer.DescriptorInfo());

		m_RayTracingDescriptorSet.Build();
	}

	for (int i = 0; i < (int)VulkanHelper::Renderer::GetMaxFramesInFlight(); i++)
	{
		std::vector<MeshAdresses> meshAddresses;
		std::vector<VulkanHelper::MaterialProperties> materials;
		auto& reg = m_CurrentSceneRendered->GetRegistry();
		auto modelView = reg.view<VulkanHelper::MeshComponent, VulkanHelper::MaterialComponent, VulkanHelper::TransformComponent>();
		uint32_t meshSizes = 0;
		uint32_t materialSizes = 0;
		for (auto& entity : modelView)
		{
			auto [meshComp, materialComp, transformComp] = reg.get<VulkanHelper::MeshComponent, VulkanHelper::MaterialComponent, VulkanHelper::TransformComponent>(entity);

			VulkanHelper::Mesh* mesh = meshComp.AssetHandle.GetMesh();
			VulkanHelper::Material* material = materialComp.AssetHandle.GetMaterial();

			MeshAdresses adr{};
			adr.VertexAddress = mesh->GetVertexBuffer()->GetDeviceAddress();
			adr.IndexAddress = mesh->GetIndexBuffer()->GetDeviceAddress();

			materials.push_back(material->Properties);
			meshAddresses.push_back(adr);
			meshSizes += sizeof(MeshAdresses);
			materialSizes += sizeof(VulkanHelper::MaterialProperties);
		}

		VL_CORE_ASSERT(meshSizes, "No meshes found?");

		m_RayTracingMeshesBuffer.WriteToBuffer(meshAddresses.data(), meshSizes, 0);
		m_RayTracingMaterialsBuffer.WriteToBuffer(materials.data(), materialSizes, 0);

		m_RayTracingMeshesBuffer.Flush(meshSizes, 0);
		m_RayTracingMaterialsBuffer.Flush(materialSizes, 0);
	}

	CreateRayTracingPipeline();
	CreateShaderBindingTable();
}

void PathTracer::CreateAccelerationStructure()
{
	VulkanHelper::AccelerationStructure::CreateInfo infoGeometry{};
	VulkanHelper::AccelerationStructure::CreateInfo infoVolumes{};

	auto view = m_CurrentSceneRendered->GetRegistry().view<VulkanHelper::MeshComponent, VulkanHelper::TransformComponent>();

	for (auto& entity : view)
	{
		auto [meshComp, TransformComp] = m_CurrentSceneRendered->GetRegistry().get<VulkanHelper::MeshComponent, VulkanHelper::TransformComponent>(entity);
		VulkanHelper::AccelerationStructure::Instance instance;
		instance.transform = TransformComp.Transform.GetKhrMat();

		VulkanHelper::Mesh* mesh = meshComp.AssetHandle.GetMesh();
		instance.mesh = mesh;

		if (m_CurrentSceneRendered->GetRegistry().try_get<VulkanHelper::MaterialComponent>(entity))
		{
			infoGeometry.Instances.push_back(instance);
		}
		else if (m_CurrentSceneRendered->GetRegistry().try_get<VolumeComponent>(entity))
		{
			infoVolumes.Instances.push_back(instance);
		}
		else
		{
			VL_ASSERT(false, "Mesh has neither material or volume component! You have to add one!");
		}
	}

	if (!infoGeometry.Instances.empty())
		m_GeometryAS.Init(infoGeometry);
	if (!infoVolumes.Instances.empty())
		m_VolumesAS.Init(infoVolumes);
}

void PathTracer::UpdateDescriptorSetsData()
{
	float newAspectRatio = (float)m_ViewportSize.width / (float)m_ViewportSize.height;
	PerspectiveCameraComponent* cameraCp = PerspectiveCameraComponent::GetMainCamera(m_CurrentSceneRendered);
	cameraCp->Camera.SetPerspectiveMatrix(cameraCp->Camera.FOV, newAspectRatio, 0.1f, 1000.0f);

	glm::mat4 flipX = glm::scale(glm::mat4(1.0f), glm::vec3(-1.0f, 1.0f, 1.0f));

	GlobalUbo ubo{};
	VL_CORE_ASSERT(cameraCp != nullptr, "No main camera found!");
	ubo.ProjInverse = glm::inverse(cameraCp->Camera.ProjMat);
	ubo.ViewInverse = glm::inverse(cameraCp->Camera.ViewMat * flipX);
	ubo.ViewProjectionMat = cameraCp->Camera.ProjMat * (cameraCp->Camera.ViewMat * flipX);
	m_GlobalSetBuffer.WriteToBuffer(&ubo);

	m_GlobalSetBuffer.Flush();

	m_RayTracingDescriptorSet.UpdateImageSampler(
		14,
		{ VulkanHelper::Renderer::GetLinearSampler().GetSamplerHandle(),
			m_GBufferAlbedo.GetImageView(),
			VK_IMAGE_LAYOUT_GENERAL }
	);

	m_RayTracingDescriptorSet.UpdateImageSampler(
		15,
		{ VulkanHelper::Renderer::GetLinearSampler().GetSamplerHandle(),
			m_GBufferNormal.GetImageView(),
			VK_IMAGE_LAYOUT_GENERAL }
	);
}

void PathTracer::BuildEnergyLookupTable()
{
	m_ReflectionEnergyLookupTable.clear();
	m_RefractionEtaGreaterThan1EnergyLookupTable.clear();
	m_RefractionEtaLessThan1EnergyLookupTable.clear();

	EnergyLossCalculator calculator;

	if (!std::filesystem::exists("assets/lookupTables/ReflectionLookup"))
	{
		// If file doesn't exists compute new values
		m_ReflectionEnergyLookupTable = std::move(calculator.CalculateReflectionEnergyLoss({ 32, 32, 32 }, 100'000));

		std::ofstream ostream("assets/lookupTables/ReflectionLookup", std::ios_base::binary | std::ios_base::trunc);
		VL_ASSERT(ostream.is_open(), "Couldn't open file for writing!");

		ostream.write((char*)m_ReflectionEnergyLookupTable.data(), m_ReflectionEnergyLookupTable.size() * 4);
	}
	else
	{
		// Else just read the cached table as recalculating it every time is quite slow even with multi threading
		std::ifstream istream("assets/lookupTables/ReflectionLookup", std::ios_base::binary);
		VL_ASSERT(istream.is_open(), "Couldn't open file for reading!");

		m_ReflectionEnergyLookupTable.resize(32 * 32 * 32);

		istream.read((char*)m_ReflectionEnergyLookupTable.data(), m_ReflectionEnergyLookupTable.size() * 4);
	}

	if (!std::filesystem::exists("assets/lookupTables/RefractionEtaGreaterThan1"))
	{
		// If file doesn't exists compute new values
		m_RefractionEtaGreaterThan1EnergyLookupTable = std::move(calculator.CalculateRefractionEnergyLoss({ 256, 64, 64 }, 10'000, true));

		std::ofstream ostream("assets/lookupTables/RefractionEtaGreaterThan1", std::ios_base::binary | std::ios_base::trunc);
		VL_ASSERT(ostream.is_open(), "Couldn't open file for writing!");

		ostream.write((char*)m_RefractionEtaGreaterThan1EnergyLookupTable.data(), m_RefractionEtaGreaterThan1EnergyLookupTable.size() * 4);
	}
	else
	{
		// Else just read the cached table as recalculating it every time is quite slow even with multi threading
		std::ifstream istream("assets/lookupTables/RefractionEtaGreaterThan1", std::ios_base::binary);
		VL_ASSERT(istream.is_open(), "Couldn't open file for reading!");

		m_RefractionEtaGreaterThan1EnergyLookupTable.resize(256 * 64 * 64);

		istream.read((char*)m_RefractionEtaGreaterThan1EnergyLookupTable.data(), m_RefractionEtaGreaterThan1EnergyLookupTable.size() * 4);
	}


	if (!std::filesystem::exists("assets/lookupTables/RefractionEtaLessThan1"))
	{
		// If file doesn't exists compute new values
		m_RefractionEtaLessThan1EnergyLookupTable = std::move(calculator.CalculateRefractionEnergyLoss({ 256, 64, 64 }, 10'000, false));

		std::ofstream ostream("assets/lookupTables/RefractionEtaLessThan1", std::ios_base::binary | std::ios_base::trunc);
		VL_ASSERT(ostream.is_open(), "Couldn't open file for writing!");

		ostream.write((char*)m_RefractionEtaLessThan1EnergyLookupTable.data(), m_RefractionEtaLessThan1EnergyLookupTable.size() * 4);
	}
	else
	{
		// Else just read the cached table as recalculating it every time is quite slow even with multi threading
		std::ifstream istream("assets/lookupTables/RefractionEtaLessThan1", std::ios_base::binary);
		VL_ASSERT(istream.is_open(), "Couldn't open file for reading!");

		m_RefractionEtaLessThan1EnergyLookupTable.resize(256 * 64 * 64);

		istream.read((char*)m_RefractionEtaLessThan1EnergyLookupTable.data(), m_RefractionEtaLessThan1EnergyLookupTable.size() * 4);
	}
}
