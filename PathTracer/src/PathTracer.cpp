// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "PathTracer.h"
#include "Components.h"
#include <glm/gtc/random.hpp>

void PathTracer::Init(VkExtent2D size)
{
	m_ViewportSize = size;

	CreateFramebuffers();

	CreateDescriptorSets();
	CreatePipelines();

	BuildEnergyLookupTable();

	Vulture::Image::CreateInfo info{};
	info.Data = m_EnergyLookupTable.data();
	info.Properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	info.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.Width = 32;
	info.Height = 32;
	info.Format = VK_FORMAT_R32_SFLOAT;
	info.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	m_LookupTexture.Init(info);
}

void PathTracer::Resize(VkExtent2D newSize)
{
	m_ViewportSize = newSize;
	Vulture::Device::WaitIdle();
	ResetFrameAccumulation();

	CreateFramebuffers();
	CreatePipelines();

	// Update Ray tracing set
	VkDescriptorImageInfo info = { 
		Vulture::Renderer::GetLinearSampler().GetSamplerHandle(), 
		m_PathTracingImage.GetImageView(), 
		VK_IMAGE_LAYOUT_GENERAL 
	};

	m_RayTracingDescriptorSet.UpdateImageSampler(
		1, 
		info
	);

	UpdateDescriptorSetsData();
}

void PathTracer::SetScene(Vulture::Scene* scene)
{
	m_CurrentSceneRendered = scene;

	auto view = scene->GetRegistry().view<SkyboxComponent>();
	SkyboxComponent* skybox = nullptr;
	for (auto& entity : view)
	{
		skybox = &scene->GetRegistry().get<SkyboxComponent>(entity);
	}

	VL_CORE_ASSERT(skybox != nullptr, "There is no skybox in the scene!");

	VkDescriptorImageInfo info = { 
		Vulture::Renderer::GetLinearSampler().GetSamplerHandle(),
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
}

bool PathTracer::Render()
{
	Vulture::Device::InsertLabel(Vulture::Renderer::GetCurrentCommandBuffer(), "Inserted label", { 0.0f, 1.0f, 0.0f, 1.0f }); // test

	if (m_PathTracingImage.GetLayout() != VK_IMAGE_LAYOUT_GENERAL)
	{
		m_PathTracingImage.TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL, Vulture::Renderer::GetCurrentCommandBuffer());
	}

	// Set Push data
	auto data = m_PushContantRayTrace.GetDataPtr();
	data->maxDepth = m_DrawInfo.RayDepth;
	data->FocalLength = m_DrawInfo.FocalLength;
	data->DoFStrength = m_DrawInfo.DOFStrength;
	data->AliasingJitter = m_DrawInfo.AliasingJitterStr;
	data->SuppressCausticsLuminance = m_DrawInfo.CausticsSuppresionMaxLuminance;
	data->SamplesPerFrame = m_DrawInfo.SamplesPerFrame;
	data->EnvAzimuth = glm::radians(m_DrawInfo.EnvAzimuth);
	data->EnvAltitude = glm::radians(m_DrawInfo.EnvAltitude);

	// Draw Albedo, Roughness, Metallness, Normal into GBuffer
	DrawGBuffer();

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
		if (m_CurrentSamplesPerPixel >= (uint32_t)m_DrawInfo.TotalSamplesPerPixel)
		{
			return false;
		}

		m_CurrentSamplesPerPixel += m_DrawInfo.SamplesPerFrame;
	}

	Vulture::Device::BeginLabel(Vulture::Renderer::GetCurrentCommandBuffer(), "Ray Trace Pass", { 1.0f, 0.0f, 0.0f, 1.0f });

	m_RtPipeline.Bind(Vulture::Renderer::GetCurrentCommandBuffer());
	m_RayTracingDescriptorSet.Bind(
		0,
		m_RtPipeline.GetPipelineLayout(),
		VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
		Vulture::Renderer::GetCurrentCommandBuffer()
	);
	m_GlobalDescriptorSets.Bind(
		1,
		m_RtPipeline.GetPipelineLayout(),
		VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
		Vulture::Renderer::GetCurrentCommandBuffer()
	);

	m_PushContantRayTrace.Push(m_RtPipeline.GetPipelineLayout(), Vulture::Renderer::GetCurrentCommandBuffer());

	Vulture::Renderer::RayTrace(Vulture::Renderer::GetCurrentCommandBuffer(), &m_SBT, m_PathTracingImage.GetImageSize());
	m_PushContantRayTrace.GetDataPtr()->frame++;

	Vulture::Device::EndLabel(Vulture::Renderer::GetCurrentCommandBuffer());

	if (m_DrawInfo.AutoDoF)
	{
		memcpy(&m_DrawInfo.FocalLength, m_RayTracingDoFBuffer.GetMappedMemory(), sizeof(float));
	}

	Vulture::PerspectiveCamera* cam = &PerspectiveCameraComponent::GetMainCamera(m_CurrentSceneRendered)->Camera;

	if (m_DrawInfo.VisualizedDOF)
	{
		auto data = m_DOfVisualizer.GetPush().GetDataPtr();
		data->Near = cam->NearFar.x;
		data->Far = cam->NearFar.y;
		data->FocalPoint = m_DrawInfo.FocalLength;
		data->VPInverse = glm::inverse(cam->GetProjView());
		m_DOfVisualizer.Run(Vulture::Renderer::GetCurrentCommandBuffer());
	}
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

	m_DrawGBuffer = true;
}

void PathTracer::RecreateRayTracingPipeline()
{
	m_RecreateRTPipeline = true;
}

void PathTracer::DrawGBuffer()
{
	if (!m_DrawGBuffer)
		return;

	Vulture::Device::BeginLabel(Vulture::Renderer::GetCurrentCommandBuffer(), "GBuffer rasterization", { 0.0f, 0.0f, 1.0f, 1.0f });

	m_DrawGBuffer = false;
	auto view = m_CurrentSceneRendered->GetRegistry().view<Vulture::MeshComponent, Vulture::TransformComponent>();
	std::vector<VkClearValue> clearColors;
	clearColors.reserve(5);
	clearColors.emplace_back(VkClearValue{ 0.0f, 0.0f, 0.0f, 0.0f });
	clearColors.emplace_back(VkClearValue{ 0.0f, 0.0f, 0.0f, 0.0f });
	clearColors.emplace_back(VkClearValue{ 0.0f, 0.0f, 0.0f, 0.0f });
	clearColors.emplace_back(VkClearValue{ 0.0f, 0.0f, 0.0f, 0.0f });
	clearColors.emplace_back(VkClearValue{ 1.0f, 0 });

	m_GBufferFramebuffer.Bind(Vulture::Renderer::GetCurrentCommandBuffer(), clearColors);
	m_GBufferPipeline.Bind(Vulture::Renderer::GetCurrentCommandBuffer());

	m_GlobalDescriptorSets.Bind
	(
		0,
		m_GBufferPipeline.GetPipelineLayout(),
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		Vulture::Renderer::GetCurrentCommandBuffer()
	);

	for (auto& entity : view)
	{
		auto [meshComp, materialComp, TransformComp] = m_CurrentSceneRendered->GetRegistry().get<Vulture::MeshComponent, Vulture::MaterialComponent, Vulture::TransformComponent>(entity);

		Vulture::MeshAsset* meshAsset = (Vulture::MeshAsset*)meshComp.AssetHandle.GetAsset();
		Vulture::Mesh* mesh = &meshAsset->Mesh;
		Vulture::Material* material = materialComp.AssetHandle.GetMaterial();
		Vulture::DescriptorSet* set = &material->Textures.TexturesSet;

		m_PushContantGBuffer.GetDataPtr()->Material = material->Properties;
		m_PushContantGBuffer.GetDataPtr()->Model = TransformComp.Transform.GetMat4();

		m_PushContantGBuffer.Push(m_GBufferPipeline.GetPipelineLayout(), Vulture::Renderer::GetCurrentCommandBuffer());

		set->Bind(1, m_GBufferPipeline.GetPipelineLayout(), VK_PIPELINE_BIND_POINT_GRAPHICS, Vulture::Renderer::GetCurrentCommandBuffer());
		mesh->Bind(Vulture::Renderer::GetCurrentCommandBuffer());
		mesh->Draw(Vulture::Renderer::GetCurrentCommandBuffer(), 1, 0);
	}

	m_GBufferFramebuffer.Unbind(Vulture::Renderer::GetCurrentCommandBuffer());

	Vulture::Device::EndLabel(Vulture::Renderer::GetCurrentCommandBuffer());
}

void PathTracer::CreateDescriptorSets()
{
	Vulture::Buffer::CreateInfo bufferInfo{};
	bufferInfo.InstanceSize = sizeof(GlobalUbo);
	bufferInfo.MemoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	bufferInfo.UsageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	m_GlobalSetBuffer.Init(bufferInfo);

	Vulture::DescriptorSetLayout::Binding bin{ 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR };
	Vulture::DescriptorSetLayout::Binding bin1{ 1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
	Vulture::DescriptorSetLayout::Binding bin2{ 2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };

	m_GlobalDescriptorSets.Init(&Vulture::Renderer::GetDescriptorPool(), { bin, bin1, bin2 }, &Vulture::Renderer::GetLinearSampler());
	m_GlobalDescriptorSets.AddBuffer(0, m_GlobalSetBuffer.DescriptorInfo());

	m_GlobalDescriptorSets.Build();
}

void PathTracer::CreatePipelines()
{
	// GBuffer
	{
		Vulture::Shader::CreateInfo shaderInfo{};
		shaderInfo.Filepath = "src/shaders/GBuffer.vert";
		shaderInfo.Type = VK_SHADER_STAGE_VERTEX_BIT;
		Vulture::Shader testShader(shaderInfo);
		{
			Vulture::DescriptorSetLayout::Binding bin1{ 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT };
			Vulture::DescriptorSetLayout::Binding bin2{ 1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT };
			Vulture::DescriptorSetLayout::Binding bin3{ 2, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT };
			Vulture::DescriptorSetLayout::Binding bin4{ 3, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT };

			Vulture::DescriptorSetLayout texturesLayout({ bin1, bin2, bin3, bin4 });

			Vulture::PushConstant<PushConstantGBuffer>::CreateInfo pushInfo{};
			pushInfo.Stage = VK_SHADER_STAGE_VERTEX_BIT;

			m_PushContantGBuffer.Init(pushInfo);

			// Configure pipeline creation parameters
			Vulture::Pipeline::GraphicsCreateInfo info{};
			info.AttributeDesc = Vulture::Mesh::Vertex::GetAttributeDescriptions();
			info.BindingDesc = Vulture::Mesh::Vertex::GetBindingDescriptions();
			Vulture::Shader shader1({ "src/shaders/GBuffer.vert", VK_SHADER_STAGE_VERTEX_BIT });

			std::vector<Vulture::Shader::Define> defines;

			Vulture::Shader shader2({ "src/shaders/GBuffer.frag", VK_SHADER_STAGE_FRAGMENT_BIT, defines });
			info.Shaders.push_back(&shader1);
			info.Shaders.push_back(&shader2);
			info.DepthTestEnable = true;
			info.Width = m_ViewportSize.width;
			info.Height = m_ViewportSize.height;
			info.PushConstants = m_PushContantGBuffer.GetRangePtr();
			info.ColorAttachmentCount = 4;
			info.RenderPass = m_GBufferFramebuffer.GetRenderPass();
			info.debugName = "GBuffer Pipeline";

			info.DescriptorSetLayouts = {
				m_GlobalDescriptorSets.GetDescriptorSetLayout()->GetDescriptorSetLayoutHandle(),
				texturesLayout.GetDescriptorSetLayoutHandle()
			};;

			m_GBufferPipeline.Init(info);
		}
	}

	// DOF
	{
		Vulture::Effect<PushConstantDOF>::CreateInfo info{};
		info.AdditionalTextures = { m_GBufferFramebuffer.GetImageNoVk(4).get() };
		info.DebugName = "DOF Visualizer";
		info.InputImage = &m_PathTracingImage;
		info.OutputImage = &m_PathTracingImage;
		info.ShaderPath = "src/shaders/DepthOfField.comp";

		m_DOfVisualizer.Init(info);
	}
}

void PathTracer::CreateRayTracingPipeline()
{
	ResetFrameAccumulation();
	{
		Vulture::PushConstant<PushConstantRay>::CreateInfo pushInfo{};
		pushInfo.Stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;

		m_PushContantRayTrace.Init(pushInfo);

		Vulture::Pipeline::RayTracingCreateInfo info{};
		info.PushConstants = m_PushContantRayTrace.GetRangePtr();

		std::vector<Vulture::Shader::Define> defines;
		defines.reserve(3);
		if (m_DrawInfo.UseCausticsSuppresion)
			defines.push_back({ "USE_CAUSTICS_SUPPRESION" });
		if (m_DrawInfo.ShowSkybox)
			defines.push_back({ "SHOW_SKYBOX" });
		if (m_DrawInfo.FurnaceTestMode)
			defines.push_back({ "FURNACE_TEST_MODE" });

		Vulture::Shader rgShader;
		if (!rgShader.Init({ m_DrawInfo.RayGenShaderPath, VK_SHADER_STAGE_RAYGEN_BIT_KHR, defines }))
			return;

		Vulture::Shader htShader;
		if (!htShader.Init({ m_DrawInfo.HitShaderPath, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, defines }))
			return;

		Vulture::Shader msShader;
		if (!msShader.Init({ m_DrawInfo.MissShaderPath, VK_SHADER_STAGE_MISS_BIT_KHR, defines }))
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
	Vulture::SBT::CreateInfo info{};
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
		Vulture::Image::CreateInfo info{};
		info.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		info.Format = VK_FORMAT_R32G32B32A32_SFLOAT;
		info.Height = m_ViewportSize.height;
		info.Width = m_ViewportSize.width;
		info.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.Properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		info.SamplerInfo = Vulture::SamplerInfo{};
		info.DebugName = "Path Tracing Image";
		m_PathTracingImage.Init(info);
		m_PathTracingImage.TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL);
	}

	// GBuffer
	{
		Vulture::Framebuffer::CreateInfo info{};
		info.AttachmentsFormats = {
			Vulture::FramebufferAttachment::ColorRGBA32, // This has to be 32 per channel otherwise optix won't work
			Vulture::FramebufferAttachment::ColorRGBA32, // This has to be 32 per channel otherwise optix won't work
			Vulture::FramebufferAttachment::ColorRG8,
			Vulture::FramebufferAttachment::ColorRGBA32,
			Vulture::FramebufferAttachment::Depth16
		};;
		info.Extent = m_ViewportSize;
		info.CustomBits = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		Vulture::Framebuffer::RenderPassCreateInfo rPassInfo{};
		info.RenderPassInfo = &rPassInfo;

		VkSubpassDependency dependency1 = {};
		dependency1.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency1.dstSubpass = 0;
		dependency1.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dependency1.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency1.srcAccessMask = 0;
		dependency1.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		VkSubpassDependency dependency2 = {};
		dependency2.srcSubpass = 0;
		dependency2.dstSubpass = VK_SUBPASS_EXTERNAL;
		dependency2.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency2.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependency2.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependency2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		info.RenderPassInfo->Dependencies = { dependency1, dependency2 };

		m_GBufferFramebuffer.Init(info);
	}
}

void PathTracer::CreateRayTracingDescriptorSets()
{
	{
		Vulture::Buffer::CreateInfo bufferInfo{};
		bufferInfo.InstanceSize = sizeof(MeshAdresses) * 50000; // TODO: dynamic amount of meshes
		bufferInfo.MemoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		bufferInfo.UsageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		m_RayTracingMeshesBuffer.Init(bufferInfo);
	}

	{
		Vulture::Buffer::CreateInfo bufferInfo{};
		bufferInfo.InstanceSize = sizeof(Vulture::MaterialProperties) * 50000; // TODO: dynamic amount of meshes
		bufferInfo.MemoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		bufferInfo.UsageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		m_RayTracingMaterialsBuffer.Init(bufferInfo);
	}

	{
		Vulture::Buffer::CreateInfo bufferInfo{};
		bufferInfo.InstanceSize = sizeof(float);
		bufferInfo.MemoryPropertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		bufferInfo.UsageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		m_RayTracingDoFBuffer.Init(bufferInfo);
		m_RayTracingDoFBuffer.Map();
	}

	uint32_t texturesCount = 0;
	{
		auto& reg = m_CurrentSceneRendered->GetRegistry();
		auto view = reg.view<Vulture::MeshComponent>();
		for (auto& entity : view)
		{
			texturesCount += 4; // Each mesh has 4 textures: albedo, normal, rough, metal
		}
	}

	{
		Vulture::DescriptorSetLayout::Binding bin0{ 0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		Vulture::DescriptorSetLayout::Binding bin1{ 1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR };
		Vulture::DescriptorSetLayout::Binding bin2{ 2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		Vulture::DescriptorSetLayout::Binding bin3{ 3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		Vulture::DescriptorSetLayout::Binding bin4{ 4, texturesCount / 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR }; // / 4 because we only care about single texture type like Albedo and not all 4 of them
		Vulture::DescriptorSetLayout::Binding bin5{ 5, texturesCount / 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		Vulture::DescriptorSetLayout::Binding bin6{ 6, texturesCount / 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		Vulture::DescriptorSetLayout::Binding bin7{ 7, texturesCount / 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		Vulture::DescriptorSetLayout::Binding bin8{ 8, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR };
		Vulture::DescriptorSetLayout::Binding bin9{ 9, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };

		m_RayTracingDescriptorSet.Init(&Vulture::Renderer::GetDescriptorPool(), { bin0, bin1, bin2, bin3, bin4, bin5, bin6, bin7, bin8, bin9 }, &Vulture::Renderer::GetLinearSampler());

		VkAccelerationStructureKHR tlas = m_AS.GetTlas().Accel;
		VkWriteDescriptorSetAccelerationStructureKHR asInfo{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
		asInfo.accelerationStructureCount = 1;
		asInfo.pAccelerationStructures = &tlas;

		m_RayTracingDescriptorSet.AddAccelerationStructure(0, asInfo);
		m_RayTracingDescriptorSet.AddImageSampler(1, { Vulture::Renderer::GetLinearSampler().GetSamplerHandle(), m_PathTracingImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL });
		m_RayTracingDescriptorSet.AddBuffer(2, m_RayTracingMeshesBuffer.DescriptorInfo());
		m_RayTracingDescriptorSet.AddBuffer(3, m_RayTracingMaterialsBuffer.DescriptorInfo());

		auto& reg = m_CurrentSceneRendered->GetRegistry();
		auto view = reg.view<Vulture::MeshComponent, Vulture::MaterialComponent>();
		for (auto& entity : view)
		{
			auto [meshComp, materialComp] = reg.get<Vulture::MeshComponent, Vulture::MaterialComponent>(entity);
			Vulture::MaterialTextures* materialTextures = &materialComp.AssetHandle.GetMaterial()->Textures;

			m_RayTracingDescriptorSet.AddImageSampler(
				4,
				{ Vulture::Renderer::GetLinearRepeatSampler().GetSamplerHandle(),
				materialTextures->AlbedoTexture.GetImage()->GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
			);

			m_RayTracingDescriptorSet.AddImageSampler(
				5,
				{ Vulture::Renderer::GetLinearRepeatSampler().GetSamplerHandle(),
				materialTextures->NormalTexture.GetImage()->GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
			);

			m_RayTracingDescriptorSet.AddImageSampler(
				6,
				{ Vulture::Renderer::GetLinearRepeatSampler().GetSamplerHandle(),
				materialTextures->RoughnessTexture.GetImage()->GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
			);

			m_RayTracingDescriptorSet.AddImageSampler(
				7,
				{ Vulture::Renderer::GetLinearRepeatSampler().GetSamplerHandle(),
				materialTextures->MetallnessTexture.GetImage()->GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
			);
		}

		m_RayTracingDescriptorSet.AddBuffer(8, m_RayTracingDoFBuffer.DescriptorInfo());

		m_RayTracingDescriptorSet.AddImageSampler(
			9, 
			{ Vulture::Renderer::GetLinearSampler().GetSamplerHandle(),
				m_LookupTexture.GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
		);

		m_RayTracingDescriptorSet.Build();
	}

	for (int i = 0; i < (int)Vulture::Renderer::GetMaxFramesInFlight(); i++)
	{
		std::vector<MeshAdresses> meshAddresses;
		std::vector<Vulture::MaterialProperties> materials;
		auto& reg = m_CurrentSceneRendered->GetRegistry();
		auto modelView = reg.view<Vulture::MeshComponent, Vulture::MaterialComponent, Vulture::TransformComponent>();
		uint32_t meshSizes = 0;
		uint32_t materialSizes = 0;
		for (auto& entity : modelView)
		{
			auto [meshComp, materialComp, transformComp] = reg.get<Vulture::MeshComponent, Vulture::MaterialComponent, Vulture::TransformComponent>(entity);

			Vulture::Mesh* mesh = meshComp.AssetHandle.GetMesh();
			Vulture::Material* material = materialComp.AssetHandle.GetMaterial();

			MeshAdresses adr{};
			adr.VertexAddress = mesh->GetVertexBuffer()->GetDeviceAddress();
			adr.IndexAddress = mesh->GetIndexBuffer()->GetDeviceAddress();

			materials.push_back(material->Properties);
			meshAddresses.push_back(adr);
			meshSizes += sizeof(MeshAdresses);
			materialSizes += sizeof(Vulture::MaterialProperties);
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
	Vulture::AccelerationStructure::CreateInfo info{};

	m_DrawGBuffer = false;
	auto view = m_CurrentSceneRendered->GetRegistry().view<Vulture::MeshComponent, Vulture::TransformComponent>();

	for (auto& entity : view)
	{
		auto [meshComp, TransformComp] = m_CurrentSceneRendered->GetRegistry().get<Vulture::MeshComponent, Vulture::TransformComponent>(entity);
		Vulture::AccelerationStructure::Instance instance;
		instance.transform = TransformComp.Transform.GetKhrMat();

		Vulture::Mesh* mesh = meshComp.AssetHandle.GetMesh();

		instance.mesh = mesh;
		info.Instances.push_back(instance);
	}

	m_AS.Init(info);
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
}

static float GGXDistributionAnisotropic(glm::vec3 H, float ax, float ay)
{
	float Hx2 = H.x * H.x;
	float Hy2 = H.y * H.y;
	float Hz2 = H.z * H.z;

	float ax2 = ax * ax;
	float ay2 = ay * ay;

	return 1.0f / M_PI * ax * ay * glm::pow(Hx2 / ax2 + Hy2 / ay2 + Hz2, 2.0f);
}

float Lambda(glm::vec3 V, float ax, float ay)
{
	float Vx2 = V.x * V.x;
	float Vy2 = V.y * V.y;
	float Vz2 = V.z * V.z;

	float ax2 = ax * ax;
	float ay2 = ay * ay;

	float nominator = -1.0f + glm::sqrt(1.0f + (ax2 * Vx2 + ay2 * Vy2) / Vz2);

	return nominator / 2.0f;
}

float GGXSmithAnisotropic(glm::vec3 V, float ax, float ay)
{
	return 1.0f / (1.0f + Lambda(V, ax, ay));
}

glm::vec3 GGXSampleAnisotopic(glm::vec3 Ve, float ax, float ay, float u1, float u2)
{
	glm::vec3 Vh = glm::normalize(glm::vec3(ax * Ve.x, ay * Ve.y, Ve.z));

	float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
	glm::vec3 T1 = lensq > 0 ? glm::vec3(-Vh.y, Vh.x, 0) * glm::inversesqrt(lensq) : glm::vec3(1, 0, 0);
	glm::vec3 T2 = glm::cross(Vh, T1);

	float r = glm::sqrt(u1);
	float phi = 2.0 * M_PI * u2;
	float t1 = r * glm::cos(phi);
	float t2 = r * glm::sin(phi);
	float s = 0.5 * (1.0 + Vh.z);
	t2 = (1.0 - s) * glm::sqrt(1.0 - t1 * t1) + s * t2;

	glm::vec3 Nh = t1 * T1 + t2 * T2 + glm::sqrt(glm::max(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;

	glm::vec3 Ne = normalize(glm::vec3(ax * Nh.x, ay * Nh.y, glm::max(0.0f, Nh.z)));

	return Ne;
}

float BRDF(glm::vec3 V, float ax, float ay)
{
	glm::vec3 H = GGXSampleAnisotopic(V, ax, ay, glm::linearRand(0.0f, 1.0f), glm::linearRand(0.0f, 1.0f));

	glm::vec3 L = reflect(-V, H);

	if (L.z < 0.0f)
		return false;

	// BRDF = D * F * GV * GL / (4.0f * NdotV)
	// 
	// PDF is VNDF / jacobian of reflect()
	// PDF = (GV * VdotH * D / NdotV) / (4.0f * VdotH)
	//
	// F = BRDF / PDF
	//
	// If we expand it we get
	// 
	//      D * F * GV * GL * 4.0f * NdotV * VdotH
	//  F = --------------------------------------
	//      4.0f * VdotH * NdotV * GV * D
	//
	// almost everything cancels out and we're only left with F * GL,
	// But since we assume that the color is 1 and only return energy lost as a
	// float we can just ignore fresnel and we're left only with GL term.

	float GL = GGXSmithAnisotropic(L, ax, ay);

	return GL;
}

void PathTracer::BuildEnergyLookupTable()
{
	m_EnergyLookupTable.clear();

	for (int i = 0; i < 32; i++)
	{
		for (int j = 0; j < 32; j++)
		{
			float roughness = float(i) / 32.0f;
			float viewCosine = glm::clamp(float(j) / 32.0f, 1e-7f, 1.0f - 1e-7f);

			int sampleCount = 10'000;
			float totalEnergy = 0.0f;

			for (int i = 0; i < sampleCount; i++)
			{
				// Generate random view dir
				float xyMagnitudeSquared = 1.0f - viewCosine * viewCosine;
				float phiV = glm::linearRand(0.0f, glm::two_pi<float>());
				float x = glm::sqrt(xyMagnitudeSquared) * glm::cos(phiV);
				float y = glm::sqrt(xyMagnitudeSquared) * glm::sin(phiV);

				// leave z as viewCosine
				float z = viewCosine;

				glm::vec3 V(x, y, z);
				V = glm::normalize(V);

				float brdf = BRDF(V, roughness, roughness);

				totalEnergy += brdf;
			}

			totalEnergy /= float(sampleCount);
			if (totalEnergy >= 0)
				totalEnergy = (1.0f - totalEnergy) / (totalEnergy);

			m_EnergyLookupTable.push_back(totalEnergy);
		}
	}
}
