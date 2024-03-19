#include "pch.h"
#include "SceneRenderer.h"

#include "imgui.h"
#include <backends/imgui_impl_vulkan.h>
#include <backends/imgui_impl_glfw.h>

#include "CameraScript.h"
#include "State.h"

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/compatibility.hpp"

SceneRenderer::SceneRenderer()
{
	//Vulture::Ref<Vulture::Image> tempImage = std::make_shared<Vulture::Image>("../Vulture/assets/black.hdr");
	//
	Vulture::Image::CreateInfo imageInfo = {};
	imageInfo.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	imageInfo.Format = VK_FORMAT_R32G32B32A32_SFLOAT;
	imageInfo.Height = 1024;
	imageInfo.Width = 1024;
	imageInfo.LayerCount = 6;
	imageInfo.Properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	imageInfo.SamplerInfo = {};
	imageInfo.Tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.Type = Vulture::Image::ImageType::Image2D;
	imageInfo.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	m_Skybox = std::make_shared<Vulture::Image>(imageInfo);
	//
	//Vulture::Renderer::EnvMapToCubemapPass(tempImage, m_Skybox, 1);

	CreateRenderPasses();

	CreateFramebuffers();

	m_BlueNoiseImage = std::make_shared<Vulture::Image>("assets/BlueNoise.png");
	m_BlueNoiseImage->TransitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	m_PaperTexture = std::make_shared<Vulture::Image>("assets/paper.png");
	m_PaperTexture->TransitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	m_InkTexture = std::make_shared<Vulture::Image>("assets/ink.png");
	m_InkTexture->TransitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	Vulture::Tonemap::CreateInfo tonemapInfo{};
	tonemapInfo.InputImages = { m_BloomImage };
	tonemapInfo.OutputImages = { m_TonemappedImage };
	m_Tonemapper.Init(tonemapInfo);

	Vulture::Bloom::CreateInfo bloomInfo{};
	bloomInfo.InputImages = { m_PathTracingImage };
	bloomInfo.OutputImages = { m_BloomImage };
	bloomInfo.MipsCount = m_DrawInfo.BloomInfo.MipCount;
	m_Bloom.Init(bloomInfo);

	bloomInfo.InputImages = { m_DenoisedImage };
	bloomInfo.OutputImages = { m_BloomImage };
	m_DenoisedBloom.Init(bloomInfo);

	Vulture::Effect<InkPushConstant>::CreateInfo inkInfo{};
	inkInfo.DebugName = "Ink Effect";
	inkInfo.InputImages = { m_TonemappedImage };
	inkInfo.OutputImages = { m_TonemappedImage };
	inkInfo.AdditionalTextures = { m_BlueNoiseImage, m_PaperTexture, m_InkTexture, m_GBufferFramebuffer->GetImageNoVk(4) };
	inkInfo.ShaderPath = "src/shaders/InkEffect.comp";
	m_InkEffect.Init(inkInfo);

	Vulture::Effect<PosterizePushConstant>::CreateInfo posterizeInfo{};
	posterizeInfo.DebugName = "Posterize Effect";
	posterizeInfo.InputImages = { m_TonemappedImage };
	posterizeInfo.OutputImages = { m_TonemappedImage };
	posterizeInfo.ShaderPath = "src/shaders/Posterize.comp";
	m_PosterizeEffect.Init(posterizeInfo);

	m_PresentedImage = m_PathTracingImage;

	CreateDescriptorSets();
	CreatePipelines();

	m_Denoiser = std::make_shared<Vulture::Denoiser>();
	m_Denoiser->Init();
	m_Denoiser->AllocateBuffers(m_ViewportContentSize);

	VkFenceCreateInfo createInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	vkCreateFence(Vulture::Device::GetDevice(), &createInfo, nullptr, &m_DenoiseFence);

	//Vulture::Renderer::RenderImGui([this](){ImGuiPass(); });
}

SceneRenderer::~SceneRenderer()
{
	vkDestroyFence(Vulture::Device::GetDevice(), m_DenoiseFence, nullptr);
	m_Denoiser->Destroy();
}

void SceneRenderer::RecreateRayTracingDescriptorSets()
{
	m_RayTracingDescriptorSet->UpdateImageSampler(1, Vulture::Renderer::GetSamplerHandle(), m_PathTracingImage->GetImageView(), VK_IMAGE_LAYOUT_GENERAL);
}

// TODO description
bool SceneRenderer::RayTrace(const glm::vec4& clearColor)
{
	Vulture::Device::InsertLabel(Vulture::Renderer::GetCurrentCommandBuffer(), "Inserted label", { 0.0f, 1.0f, 0.0f, 1.0f });

	m_PushContantRayTrace.GetDataPtr()->ClearColor = clearColor;
	m_PushContantRayTrace.GetDataPtr()->maxDepth = m_DrawInfo.RayDepth;
	m_PushContantRayTrace.GetDataPtr()->FocalLength = m_DrawInfo.FocalLength;
	m_PushContantRayTrace.GetDataPtr()->DoFStrength = m_DrawInfo.DOFStrength;
	m_PushContantRayTrace.GetDataPtr()->AliasingJitter = m_DrawInfo.AliasingJitterStr;
	m_PushContantRayTrace.GetDataPtr()->SamplesPerFrame = m_DrawInfo.SamplesPerFrame;
	m_PushContantRayTrace.GetDataPtr()->EnvAzimuth =  glm::radians(m_DrawInfo.EnvAzimuth);
	m_PushContantRayTrace.GetDataPtr()->EnvAltitude = glm::radians(m_DrawInfo.EnvAltitude);

	// Draw Albedo, Roughness, Metallness, Normal into GBuffer
	DrawGBuffer();

	static glm::mat4 previousMat{ 0.0f };
	if (previousMat != m_CurrentSceneRendered->GetMainCamera()->ViewMat) // if camera moved
	{
		UpdateDescriptorSetsData();
		ResetFrameAccumulation();
		previousMat = m_CurrentSceneRendered->GetMainCamera()->ViewMat;
	}
	else
	{
		if (m_CurrentSamplesPerPixel >= (uint32_t)m_DrawInfo.TotalSamplesPerPixel)
		{
			return false;
		}

		m_PushContantRayTrace.GetDataPtr()->frame++;
		m_CurrentSamplesPerPixel += m_DrawInfo.SamplesPerFrame;
	}

	Vulture::Device::BeginLabel(Vulture::Renderer::GetCurrentCommandBuffer(), "Ray Trace Pass", { 1.0f, 0.0f, 0.0f, 1.0f });

	m_RtPipeline.Bind(Vulture::Renderer::GetCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR);
	m_RayTracingDescriptorSet->Bind(
		0,
		m_RtPipeline.GetPipelineLayout(),
		VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
		Vulture::Renderer::GetCurrentCommandBuffer()
	);
	m_GlobalDescriptorSets[Vulture::Renderer::GetCurrentFrameIndex()]->Bind(
		1,
		m_RtPipeline.GetPipelineLayout(),
		VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
		Vulture::Renderer::GetCurrentCommandBuffer()
	);

	m_PushContantRayTrace.Push(m_RtPipeline.GetPipelineLayout(), Vulture::Renderer::GetCurrentCommandBuffer());

	Vulture::Renderer::RayTrace(Vulture::Renderer::GetCurrentCommandBuffer(), &m_SBT, m_PathTracingImage->GetImageSize());

	Vulture::Device::EndLabel(Vulture::Renderer::GetCurrentCommandBuffer());

	if (m_DrawInfo.AutoDoF)
	{
		memcpy(&m_DrawInfo.FocalLength, m_RayTracingDescriptorSet->GetBuffer(8)->GetMappedMemory(), sizeof(float));
	}

	return true;
}

void SceneRenderer::PostProcess()
{
	if (State::ShowDenoised)
		m_DenoisedBloom.Run(m_DrawInfo.BloomInfo, Vulture::Renderer::GetCurrentCommandBuffer());
	else
		m_Bloom.Run(m_DrawInfo.BloomInfo, Vulture::Renderer::GetCurrentCommandBuffer());

	m_Tonemapper.Run(m_DrawInfo.TonemapInfo, Vulture::Renderer::GetCurrentCommandBuffer());

	if (m_CurrentEffect == PostProcessEffects::Ink)
	{
		if (m_GBufferFramebuffer->GetImageNoVk(4)->GetLayout() != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			m_GBufferFramebuffer->GetImageNoVk(4)->TransitionImageLayout(
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				Vulture::Renderer::GetCurrentCommandBuffer(),
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				0,
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				0
			);
		}
		m_InkEffect.Run(Vulture::Renderer::GetCurrentCommandBuffer());
	}
	else if (m_CurrentEffect == PostProcessEffects::Posterize)
	{
		m_PosterizeEffect.Run(Vulture::Renderer::GetCurrentCommandBuffer());
	}

	m_TonemappedImage->TransitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, Vulture::Renderer::GetCurrentCommandBuffer());
}

void SceneRenderer::DrawGBuffer()
{
	if (!m_DrawGBuffer)
		return;

	Vulture::Device::BeginLabel(Vulture::Renderer::GetCurrentCommandBuffer(), "GBuffer rasterization", { 0.0f, 0.0f, 1.0f, 1.0f });

	m_DrawGBuffer = false;
	auto view = m_CurrentSceneRendered->GetRegistry().view<Vulture::ModelComponent, Vulture::TransformComponent>();
	std::vector<VkClearValue> clearColors;
	clearColors.push_back({ 0.0f, 0.0f, 0.0f, 0.0f });
	clearColors.push_back({ 0.0f, 0.0f, 0.0f, 0.0f });
	clearColors.push_back({ 0.0f, 0.0f, 0.0f, 0.0f });
	clearColors.push_back({ 0.0f, 0.0f, 0.0f, 0.0f });
	VkClearValue clearVal{};
	clearVal.depthStencil = { 1.0f, 1 };
	clearColors.push_back(clearVal);

	m_GBufferFramebuffer->Bind(Vulture::Renderer::GetCurrentCommandBuffer(), clearColors);
	m_GBufferPipeline.Bind(Vulture::Renderer::GetCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS);

	m_GlobalDescriptorSets[Vulture::Renderer::GetCurrentFrameIndex()]->Bind
	(
		0,
		m_GBufferPipeline.GetPipelineLayout(),
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		Vulture::Renderer::GetCurrentCommandBuffer()
	);


	for (auto& entity : view)
	{
		auto& [modelComp, TransformComp] = m_CurrentSceneRendered->GetRegistry().get<Vulture::ModelComponent, Vulture::TransformComponent>(entity);
		
		std::vector<Vulture::Ref<Vulture::Mesh>> meshes = modelComp.Model->GetMeshes();
		std::vector<Vulture::Ref<Vulture::DescriptorSet>> sets = modelComp.Model->GetDescriptors();
		for (uint32_t i = 0; i < modelComp.Model->GetMeshCount(); i++)
		{
			m_PushContantGBuffer.GetDataPtr()->Material = modelComp.Model->GetMaterial(i);
			m_PushContantGBuffer.GetDataPtr()->Model = TransformComp.transform.GetMat4();
			
			m_PushContantGBuffer.Push(m_GBufferPipeline.GetPipelineLayout(), Vulture::Renderer::GetCurrentCommandBuffer());

			sets[i]->Bind(1, m_GBufferPipeline.GetPipelineLayout(), VK_PIPELINE_BIND_POINT_GRAPHICS, Vulture::Renderer::GetCurrentCommandBuffer());
			meshes[i]->Bind(Vulture::Renderer::GetCurrentCommandBuffer());
			meshes[i]->Draw(Vulture::Renderer::GetCurrentCommandBuffer(), 1, 0);
		}
	}

	m_GBufferFramebuffer->Unbind(Vulture::Renderer::GetCurrentCommandBuffer());

	Vulture::Device::EndLabel(Vulture::Renderer::GetCurrentCommandBuffer());
}

void SceneRenderer::Denoise()
{
	VkCommandBuffer cmd = Vulture::Renderer::GetCurrentCommandBuffer();

	// copy images to cuda buffers
	std::vector<Vulture::Image*> vec = 
	{
		&(*m_PathTracingImage), // Path Tracing Result
		const_cast<Vulture::Image*>(&(*m_GBufferFramebuffer->GetImageNoVk(GBufferImage::Albedo))), // Albedo
		const_cast<Vulture::Image*>(&(*m_GBufferFramebuffer->GetImageNoVk(GBufferImage::Normal))) // Normal
	};

	vkEndCommandBuffer(cmd);

	// Increment for signaling
	m_DenoiseFenceValue++;

	VkSubmitInfo submits{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submits.commandBufferCount = 1;
	submits.pCommandBuffers = &cmd;

	submits.signalSemaphoreCount = 1;
	VkSemaphore sem = m_Denoiser->GetTLSemaphore();
	submits.pSignalSemaphores = &sem;

	VkTimelineSemaphoreSubmitInfo timelineSubmitInfo{};
	timelineSubmitInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
	timelineSubmitInfo.pSignalSemaphoreValues = &m_DenoiseFenceValue;
	timelineSubmitInfo.signalSemaphoreValueCount = 1;
	timelineSubmitInfo.waitSemaphoreValueCount = 0;

	submits.pNext = &timelineSubmitInfo;

	// Submit command buffer so that resources can be used by Optix and CUDA
	vkResetFences(Vulture::Device::GetDevice(), 1, &m_DenoiseFence);
	vkQueueSubmit(Vulture::Device::GetGraphicsQueue(), 1, &submits, m_DenoiseFence);

	VkResult res = vkWaitForFences(Vulture::Device::GetDevice(), 1, &m_DenoiseFence, VK_TRUE, UINT64_MAX);

	// TODO: don't stall here
	VkCommandBuffer singleCmd;
	Vulture::Device::BeginSingleTimeCommands(singleCmd, Vulture::Device::GetGraphicsCommandPool());
	m_Denoiser->ImageToBuffer(singleCmd, vec);
	Vulture::Device::EndSingleTimeCommands(singleCmd, Vulture::Device::GetGraphicsQueue(), Vulture::Device::GetGraphicsCommandPool());

	// Run Denoiser
	m_Denoiser->DenoiseImageBuffer(m_DenoiseFenceValue, 0.0f);

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	
	// Start recording command buffer
	vkBeginCommandBuffer(cmd, &beginInfo);

	// Copy Optix denoised image to m_DenoisedImage
	m_Denoiser->BufferToImage(cmd, &(*m_DenoisedImage));

	VkMemoryBarrier memBarrier{};
	memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	// Wait for copying to finish
	vkCmdPipelineBarrier(
		cmd,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0,
		1, &memBarrier,
		0, nullptr,
		0, nullptr);
}

void SceneRenderer::ResetFrameAccumulation()
{
	m_PushContantRayTrace.GetDataPtr()->frame = -1;
	m_CurrentSamplesPerPixel = 0;

	m_DrawGBuffer = true;
}

void SceneRenderer::RecreateResources()
{
	vkDeviceWaitIdle(Vulture::Device::GetDevice());
	ResetFrameAccumulation();
	m_PushContantRayTrace.GetDataPtr()->frame = -1;

	CreateFramebuffers();

	Vulture::Tonemap::CreateInfo tonemapInfo{};
	tonemapInfo.InputImages = { m_BloomImage };
	tonemapInfo.OutputImages = { m_TonemappedImage };
	m_Tonemapper.Init(tonemapInfo);

	Vulture::Bloom::CreateInfo bloomInfo{};
	bloomInfo.InputImages = { m_PathTracingImage };
	bloomInfo.OutputImages = { m_BloomImage };
	bloomInfo.MipsCount = m_DrawInfo.BloomInfo.MipCount;
	m_Bloom.Init(bloomInfo);

	bloomInfo.InputImages = { m_DenoisedImage };
	bloomInfo.OutputImages = { m_BloomImage };
	m_DenoisedBloom.Init(bloomInfo);

	Vulture::Effect<InkPushConstant>::CreateInfo inkInfo{};
	inkInfo.DebugName = "Stippling";
	inkInfo.InputImages = { m_TonemappedImage };
	inkInfo.OutputImages = { m_TonemappedImage };
	inkInfo.AdditionalTextures = { m_BlueNoiseImage, m_PaperTexture, m_InkTexture, m_GBufferFramebuffer->GetImageNoVk(4) };
	inkInfo.ShaderPath = "src/shaders/InkEffect.comp";
	m_InkEffect.Init(inkInfo);

	Vulture::Effect<PosterizePushConstant>::CreateInfo posterizeInfo{};
	posterizeInfo.DebugName = "Posterize Effect";
	posterizeInfo.InputImages = { m_TonemappedImage };
	posterizeInfo.OutputImages = { m_TonemappedImage };
	posterizeInfo.ShaderPath = "src/shaders/Posterize.comp";
	if (State::ReplacePalletDefineInPosterizeShader)
		posterizeInfo.Defines = { "REPLACE_PALLET" };
	
	m_PosterizeEffect.Init(posterizeInfo);

	CreatePipelines();

	UpdateCamera();

	m_Denoiser->AllocateBuffers(m_ViewportContentSize);

	RecreateDescriptorSets();
}

void SceneRenderer::UpdateCamera()
{
	float newAspectRatio = (float)m_ViewportContentSize.width / (float)m_ViewportContentSize.height;
	auto view = m_CurrentSceneRendered->GetRegistry().view<Vulture::CameraComponent>();
	for (auto entity : view)
	{
		auto& cameraCp = view.get<Vulture::CameraComponent>(entity);
		cameraCp.SetPerspectiveMatrix(cameraCp.FOV, newAspectRatio, 0.1f, 1000.0f);
	}
	UpdateDescriptorSetsData();
}

void SceneRenderer::CreateRenderPasses()
{
	// none for now
}

void SceneRenderer::CreateDescriptorSets()
{
	m_GlobalDescriptorSets.clear();

	// Global Set
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		Vulture::DescriptorSetLayout::Binding bin{ 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR };
		Vulture::DescriptorSetLayout::Binding bin1{ 1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		Vulture::DescriptorSetLayout::Binding bin2{ 2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };

		m_GlobalDescriptorSets.push_back(std::make_shared<Vulture::DescriptorSet>());
		m_GlobalDescriptorSets[i]->Init(&Vulture::Renderer::GetDescriptorPool(), { bin, bin1, bin2 });
		m_GlobalDescriptorSets[i]->AddUniformBuffer(0, sizeof(GlobalUbo));

		m_GlobalDescriptorSets[i]->AddImageSampler(
			1,
			Vulture::Renderer::GetSamplerHandle(),
			Vulture::Renderer::GetEnv()->GetImageView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		);

		m_GlobalDescriptorSets[i]->AddStorageBuffer(2, (uint32_t)Vulture::Renderer::GetEnv()->GetAccelBuffer()->GetBufferSize(), true, true);

		m_GlobalDescriptorSets[i]->Build();

		Vulture::Buffer::CopyBuffer(
			Vulture::Renderer::GetEnv()->GetAccelBuffer()->GetBuffer(),
			m_GlobalDescriptorSets[i]->GetBuffer(2)->GetBuffer(),
			Vulture::Renderer::GetEnv()->GetAccelBuffer()->GetBufferSize(),
			0, 0,
			Vulture::Device::GetGraphicsQueue(),
			0,
			Vulture::Device::GetGraphicsCommandPool()
		);
	}

	
#ifdef VL_IMGUI

	m_ImGuiAlbedoDescriptor = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetSamplerHandle(), m_GBufferFramebuffer->GetImageView(GBufferImage::Albedo), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	m_ImGuiNormalDescriptor = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetSamplerHandle(), m_GBufferFramebuffer->GetImageView(GBufferImage::Normal), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	m_ImGuiRoughnessDescriptor = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetSamplerHandle(), m_GBufferFramebuffer->GetImageView(GBufferImage::RoughnessMetallness), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	m_ImGuiEmissiveDescriptor = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetSamplerHandle(), m_GBufferFramebuffer->GetImageView(GBufferImage::Emissive), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	m_ImGuiViewportDescriptorTonemapped = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetSamplerHandle(), m_TonemappedImage->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	m_ImGuiViewportDescriptorPathTracing = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetSamplerHandle(), m_PathTracingImage->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

#endif
}

void SceneRenderer::CreateRayTracingDescriptorSets()
{
	uint32_t texturesCount = m_CurrentSceneRendered->GetMeshCount();
	{
		Vulture::DescriptorSetLayout::Binding bin0{ 0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		Vulture::DescriptorSetLayout::Binding bin1{ 1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR };
		Vulture::DescriptorSetLayout::Binding bin2{ 2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		Vulture::DescriptorSetLayout::Binding bin3{ 3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		Vulture::DescriptorSetLayout::Binding bin4{ 4, texturesCount, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		Vulture::DescriptorSetLayout::Binding bin5{ 5, texturesCount, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		Vulture::DescriptorSetLayout::Binding bin6{ 6, texturesCount, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		Vulture::DescriptorSetLayout::Binding bin7{ 7, texturesCount, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
		Vulture::DescriptorSetLayout::Binding bin8{ 8, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR };
		
		m_RayTracingDescriptorSet = std::make_shared<Vulture::DescriptorSet>();
		m_RayTracingDescriptorSet->Init(&Vulture::Renderer::GetDescriptorPool(), { bin0, bin1, bin2, bin3, bin4, bin5, bin6, bin7, bin8 });

		VkAccelerationStructureKHR tlas = m_CurrentSceneRendered->GetAccelerationStructure()->GetTlas().Accel;
		VkWriteDescriptorSetAccelerationStructureKHR asInfo{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
		asInfo.accelerationStructureCount = 1;
		asInfo.pAccelerationStructures = &tlas;

		m_RayTracingDescriptorSet->AddAccelerationStructure(0, asInfo);
		m_RayTracingDescriptorSet->AddImageSampler(1, Vulture::Renderer::GetSamplerHandle(), m_PathTracingImage->GetImageView(), VK_IMAGE_LAYOUT_GENERAL);
		m_RayTracingDescriptorSet->AddStorageBuffer(2, sizeof(MeshAdresses) * 50000, false, true);
		m_RayTracingDescriptorSet->AddStorageBuffer(3, sizeof(Vulture::Material) * 50000, false, true);

		auto view = m_CurrentSceneRendered->GetRegistry().view<Vulture::ModelComponent>();
		for (auto& entity : view)
		{
			auto& modelComp = m_CurrentSceneRendered->GetRegistry().get<Vulture::ModelComponent>(entity);
			for (int j = 0; j < (int)modelComp.Model->GetAlbedoTextureCount(); j++)
			{
				m_RayTracingDescriptorSet->AddImageSampler(
					4,
					Vulture::Renderer::GetSamplerHandle(),
					modelComp.Model->GetAlbedoTexture(j)->GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				);
			}
			for (int j = 0; j < (int)modelComp.Model->GetNormalTextureCount(); j++)
			{
				m_RayTracingDescriptorSet->AddImageSampler(
					5,
					Vulture::Renderer::GetSamplerHandle(),
					modelComp.Model->GetNormalTexture(j)->GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				);
			}
			for (int j = 0; j < (int)modelComp.Model->GetRoughnessTextureCount(); j++)
			{
				m_RayTracingDescriptorSet->AddImageSampler(
					6,
					Vulture::Renderer::GetSamplerHandle(),
					modelComp.Model->GetRoughnessTexture(j)->GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				);
			}
			for (int j = 0; j < (int)modelComp.Model->GetMetallnessTextureCount(); j++)
			{
				m_RayTracingDescriptorSet->AddImageSampler(
					7,
					Vulture::Renderer::GetSamplerHandle(),
					modelComp.Model->GetMetallnessTexture(j)->GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				);
			}
			m_RayTracingDescriptorSet->AddStorageBuffer(8, sizeof(float));
		}
		
		m_RayTracingDescriptorSet->Build();
	}

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		std::vector<MeshAdresses> meshAddresses;
		std::vector<Vulture::Material> materials;
		auto modelView = m_CurrentSceneRendered->GetRegistry().view<Vulture::ModelComponent, Vulture::TransformComponent>();
		uint32_t meshSizes = 0;
		uint32_t materialSizes = 0;
		for (auto& entity : modelView)
		{
			auto& [modelComp, transformComp] = m_CurrentSceneRendered->GetRegistry().get<Vulture::ModelComponent, Vulture::TransformComponent>(entity);
			for (int i = 0; i < (int)modelComp.Model->GetMeshCount(); i++)
			{
				MeshAdresses adr{};
				adr.VertexAddress = modelComp.Model->GetMesh(i).GetVertexBuffer()->GetDeviceAddress();
				adr.IndexAddress = modelComp.Model->GetMesh(i).GetIndexBuffer()->GetDeviceAddress();

				Vulture::Material material = modelComp.Model->GetMaterial(i);

				materials.push_back(material);
				meshAddresses.push_back(adr);
			}
			meshSizes += sizeof(MeshAdresses) * modelComp.Model->GetMeshCount();
			materialSizes += sizeof(Vulture::Material) * modelComp.Model->GetMeshCount();
		}

		VL_CORE_ASSERT(meshSizes, "No meshes found?");

		m_RayTracingDescriptorSet->GetBuffer(2)->WriteToBuffer(meshAddresses.data(), meshSizes, 0);
		m_RayTracingDescriptorSet->GetBuffer(3)->WriteToBuffer(materials.data(), materialSizes, 0);

		m_RayTracingDescriptorSet->GetBuffer(2)->Flush(meshSizes, 0);
		m_RayTracingDescriptorSet->GetBuffer(3)->Flush(materialSizes, 0);
	}

	CreateRayTracingPipeline();
	CreateShaderBindingTable();
}

void SceneRenderer::SetSkybox(Vulture::Entity& skyboxEntity)
{
	State::CurrentSkyboxEntity = skyboxEntity;
	Vulture::SkyboxComponent skybox = skyboxEntity.GetComponent<Vulture::SkyboxComponent>();

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		m_GlobalDescriptorSets[i]->UpdateImageSampler(
			1,
			Vulture::Renderer::GetSamplerHandle(),
			skybox.SkyboxImage->GetImageView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		);

		// Resize env map accel info because env map is probably different size
		m_GlobalDescriptorSets[i]->Resize(2, skybox.SkyboxImage->GetAccelBuffer()->GetBufferSize());

		Vulture::Buffer::CopyBuffer(
			skybox.SkyboxImage->GetAccelBuffer()->GetBuffer(),
			m_GlobalDescriptorSets[i]->GetBuffer(2)->GetBuffer(),
			skybox.SkyboxImage->GetAccelBuffer()->GetBufferSize(),
			0, 0,
			Vulture::Device::GetGraphicsQueue(),
			0,
			Vulture::Device::GetGraphicsCommandPool()
		);
	}

	State::RecreateRayTracingPipeline = true;
	m_HasEnvMap = true;
}

void SceneRenderer::UpdateResources()
{
	if (State::RecreateRayTracingPipeline)
	{
		vkDeviceWaitIdle(Vulture::Device::GetDevice());
		ResetFrameAccumulation();
		CreateRayTracingPipeline();
		State::RecreateRayTracingPipeline = false;
	}

	if (State::ChangeSkybox)
	{
		State::ChangeSkybox = false;
		vkDeviceWaitIdle(Vulture::Device::GetDevice());

		m_CurrentSceneRendered->DestroyEntity(State::CurrentSkyboxEntity);

		State::CurrentSkyboxEntity = m_CurrentSceneRendered->CreateEntity();
		auto& skyboxComponent = State::CurrentSkyboxEntity.AddComponent<Vulture::SkyboxComponent>(State::CurrentSkyboxPath);

		SetSkybox(State::CurrentSkyboxEntity);
	}

	if (State::ModelChanged)
	{
		State::ModelChanged = false;
		vkDeviceWaitIdle(Vulture::Device::GetDevice());

		auto view = m_CurrentSceneRendered->GetRegistry().view<Vulture::ModelComponent>();
		for (auto entity : view)
		{
			m_CurrentSceneRendered->GetRegistry().destroy(entity);
		}
		m_CurrentSceneRendered->ResetMeshCount();
		m_CurrentSceneRendered->ResetModelCount();
		m_CurrentSceneRendered->ResetVertexCount();
		m_CurrentSceneRendered->ResetIndexCount();

		State::CurrentModelEntity = m_CurrentSceneRendered->CreateModel(State::ModelPath, Vulture::Transform(glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(State::ModelScale)));

		m_CurrentSceneRendered->InitAccelerationStructure();
		CreateRayTracingDescriptorSets();

		ResetFrameAccumulation();
	}

	if (m_RecompileTonemapShader)
	{
		vkDeviceWaitIdle(Vulture::Device::GetDevice());
		m_Tonemapper.RecompileShader(State::CurrentTonemapper, State::UseChromaticAberration);
		m_RecompileTonemapShader = false;
	}

	if (State::RecompilePosterizeShader)
	{
		vkDeviceWaitIdle(Vulture::Device::GetDevice());
		std::vector<std::string> define;
		if (State::ReplacePalletDefineInPosterizeShader) { define.push_back("REPLACE_PALLET"); }
		m_PosterizeEffect.RecompileShader(define);
		State::RecompilePosterizeShader = false;
	}
}

void SceneRenderer::SetCurrentScene(Vulture::Scene* scene)
{
	m_CurrentSceneRendered = scene;
	UpdateDescriptorSetsData();
}

void SceneRenderer::RecreateDescriptorSets()
{
	State::Denoised = false;
	State::ShowDenoised = false;

	{
		m_RayTracingDescriptorSet->UpdateImageSampler(
			1,
			Vulture::Renderer::GetSamplerHandle(),
			m_PathTracingImage->GetImageView(),
			VK_IMAGE_LAYOUT_GENERAL
		);
	}

	RecreateRayTracingDescriptorSets();

#ifdef VL_IMGUI
	ImGui_ImplVulkan_RemoveTexture(m_ImGuiAlbedoDescriptor);
	ImGui_ImplVulkan_RemoveTexture(m_ImGuiNormalDescriptor);
	ImGui_ImplVulkan_RemoveTexture(m_ImGuiRoughnessDescriptor);
	ImGui_ImplVulkan_RemoveTexture(m_ImGuiEmissiveDescriptor);
	ImGui_ImplVulkan_RemoveTexture(m_ImGuiViewportDescriptorTonemapped);
	ImGui_ImplVulkan_RemoveTexture(m_ImGuiViewportDescriptorPathTracing);

	m_ImGuiAlbedoDescriptor = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetSamplerHandle(), m_GBufferFramebuffer->GetImageView(GBufferImage::Albedo), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	m_ImGuiNormalDescriptor = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetSamplerHandle(), m_GBufferFramebuffer->GetImageView(GBufferImage::Normal), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	m_ImGuiRoughnessDescriptor = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetSamplerHandle(), m_GBufferFramebuffer->GetImageView(GBufferImage::RoughnessMetallness), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	m_ImGuiEmissiveDescriptor = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetSamplerHandle(), m_GBufferFramebuffer->GetImageView(GBufferImage::Emissive), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	m_ImGuiViewportDescriptorTonemapped = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetSamplerHandle(), m_TonemappedImage->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	m_ImGuiViewportDescriptorPathTracing = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetSamplerHandle(), m_PathTracingImage->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

#endif
}

void SceneRenderer::CreatePipelines()
{
	
}

void SceneRenderer::CreateRayTracingPipeline()
{
	vkDeviceWaitIdle(Vulture::Device::GetDevice());
	//
	// Ray Tracing
	//
	{
		Vulture::PushConstant<PushConstantRay>::CreateInfo pushInfo{};
		pushInfo.Stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
		
		m_PushContantRayTrace.Init(pushInfo);

		Vulture::Pipeline::RayTracingCreateInfo info{};
		info.PushConstants = m_PushContantRayTrace.GetRangePtr();

		std::vector<std::string> defines;
		if (m_DrawInfo.UseAlbedo)
			defines.push_back("USE_ALBEDO");
		if (m_DrawInfo.UseNormalMaps)
			defines.push_back("USE_NORMAL_MAPS");
		if (m_DrawInfo.SampleEnvMap)
			defines.push_back("SAMPLE_ENV_MAP");
		if (m_DrawInfo.UseGlossy)
			defines.push_back("USE_GLOSSY");
		if (m_DrawInfo.UseGlass)
			defines.push_back("USE_GLASS");
		if (m_DrawInfo.UseClearcoat)
			defines.push_back("USE_CLEARCOAT");
		if (m_DrawInfo.UseFireflies)
			defines.push_back("USE_FIREFLIES");
		if (m_DrawInfo.ShowSkybox)
			defines.push_back("SHOW_SKYBOX");

		Vulture::Shader shader1({ "src/shaders/raytrace.rgen", VK_SHADER_STAGE_RAYGEN_BIT_KHR, defines });
		Vulture::Shader shader2({ "src/shaders/raytrace.rchit", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, defines});
		Vulture::Shader shader3({ "src/shaders/raytrace.rmiss", VK_SHADER_STAGE_MISS_BIT_KHR, defines });

		info.RayGenShaders.push_back(&shader1);
		info.HitShaders.push_back(&shader2);
		info.MissShaders.push_back(&shader3);

		// Descriptor set layouts for the pipeline
		std::vector<VkDescriptorSetLayout> layouts
		{
			m_RayTracingDescriptorSet->GetDescriptorSetLayout()->GetDescriptorSetLayoutHandle(),
			m_GlobalDescriptorSets[0]->GetDescriptorSetLayout()->GetDescriptorSetLayoutHandle()
		};
		info.DescriptorSetLayouts = layouts;
		info.debugName = "Ray Tracing Pipeline";

		m_RtPipeline.Init(info);
	}

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

			std::vector<std::string> defines;
			if (m_DrawInfo.UseAlbedo)
				defines.push_back("USE_ALBEDO");
			if (m_DrawInfo.UseNormalMaps)
				defines.push_back("USE_NORMAL_MAPS");
			if (m_DrawInfo.SampleEnvMap)
				defines.push_back("SAMPLE_ENV_MAP");

			Vulture::Shader shader2({ "src/shaders/GBuffer.frag", VK_SHADER_STAGE_FRAGMENT_BIT, defines });
			info.Shaders.push_back(&shader1);
			info.Shaders.push_back(&shader2);
			info.BlendingEnable = false;
			info.DepthTestEnable = true;
			info.CullMode = VK_CULL_MODE_NONE;
			info.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			info.Width = m_ViewportContentSize.width;
			info.Height = m_ViewportContentSize.height;
			info.PushConstants = m_PushContantGBuffer.GetRangePtr();
			info.ColorAttachmentCount = 4;
			info.RenderPass = m_GBufferFramebuffer->GetRenderPass();
			info.debugName = "GBuffer Pipeline";

			// Descriptor set layouts for the pipeline
			std::vector<VkDescriptorSetLayout> layouts
			{
				m_GlobalDescriptorSets[0]->GetDescriptorSetLayout()->GetDescriptorSetLayoutHandle(),
				texturesLayout.GetDescriptorSetLayoutHandle()
			};
			info.DescriptorSetLayouts = layouts;

			m_GBufferPipeline.Init(info);
		}
	}
}

void SceneRenderer::CreateShaderBindingTable()
{
	Vulture::SBT::CreateInfo info{};
	info.CallableCount = 0;
	info.HitCount = 1;
	info.MissCount = 1;
	info.RGenCount = 1;
	info.RayTracingPipeline = &m_RtPipeline;

	m_SBT.Init(&info);
}

void SceneRenderer::CreateFramebuffers()
{
	// Path Tracing
	{
		Vulture::Image::CreateInfo info{};
		info.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		info.Format = VK_FORMAT_R32G32B32A32_SFLOAT;
		info.Height = m_ViewportContentSize.height;
		info.Width = m_ViewportContentSize.width;
		info.LayerCount = 1;
		info.Tiling = VK_IMAGE_TILING_OPTIMAL;
		info.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.Properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		info.SamplerInfo = Vulture::SamplerInfo{};
		info.Type = Vulture::Image::ImageType::Image2D;
		info.DebugName = "Path Tracing Image";
		m_PathTracingImage = std::make_shared<Vulture::Image>(info);
		m_PathTracingImage->TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL);
	}

	// Denoised
	{
		Vulture::Image::CreateInfo info{};
		info.Width = m_ViewportContentSize.width;
		info.Height = m_ViewportContentSize.height;
		info.Format = VK_FORMAT_R32G32B32A32_SFLOAT;
		info.Tiling = VK_IMAGE_TILING_OPTIMAL;
		info.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.Properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		info.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		info.LayerCount = 1;
		info.SamplerInfo = Vulture::SamplerInfo{};
		info.Type = Vulture::Image::ImageType::Image2D;
		info.DebugName = "Denoised Image";

		m_DenoisedImage = std::make_shared<Vulture::Image>(info);
		m_DenoisedImage->TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL);
	}

	// GBuffer
	{
		std::vector<Vulture::FramebufferAttachment> attachments
		{ 
			Vulture::FramebufferAttachment::ColorRGBA32, // This has to be 32 per channel otherwise optix won't work
			Vulture::FramebufferAttachment::ColorRGBA32, // This has to be 32 per channel otherwise optix won't work
			Vulture::FramebufferAttachment::ColorRG8,
			Vulture::FramebufferAttachment::ColorRGBA32,
			Vulture::FramebufferAttachment::Depth
		};
		
		Vulture::Framebuffer::CreateInfo info{};
		info.AttachmentsFormats = &attachments;
		info.DepthFormat = Vulture::Swapchain::FindDepthFormat();
		info.Extent = m_ViewportContentSize;
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

		std::vector<VkSubpassDependency> dependencies{ dependency1, dependency2 };
		info.RenderPassInfo->Dependencies = dependencies;

		m_GBufferFramebuffer = std::make_shared<Vulture::Framebuffer>(info);
	}

	// Tonemapped
	{
		Vulture::Image::CreateInfo info{};
		info.Width = m_ViewportContentSize.width;
		info.Height = m_ViewportContentSize.height;
		info.Format = VK_FORMAT_R8G8B8A8_UNORM;
		info.Tiling = VK_IMAGE_TILING_OPTIMAL;
		info.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		info.Properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		info.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		info.LayerCount = 1;
		info.SamplerInfo = Vulture::SamplerInfo{};
		info.Type = Vulture::Image::ImageType::Image2D;
		info.DebugName = "Tonemapped Image";

		m_TonemappedImage = std::make_shared<Vulture::Image>();
		m_TonemappedImage->Init(info);
	}

	// Bloom
	{
		Vulture::Image::CreateInfo info{};
		info.Width = m_ViewportContentSize.width;
		info.Height = m_ViewportContentSize.height;
		info.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
		info.Tiling = VK_IMAGE_TILING_OPTIMAL;
		info.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; // TODO: check all usages in general, especially here, the transfer one
		info.Properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		info.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		info.LayerCount = 1;
		info.SamplerInfo = Vulture::SamplerInfo{};
		info.Type = Vulture::Image::ImageType::Image2D;
		info.DebugName = "Bloom Image";

		m_BloomImage = std::make_shared<Vulture::Image>();
		m_BloomImage->Init(info);
	}
}

void SceneRenderer::UpdateDescriptorSetsData()
{
	auto cameraView = m_CurrentSceneRendered->GetRegistry().view<Vulture::CameraComponent>();
	Vulture::CameraComponent* camComp = nullptr;
	for (auto& entity : cameraView)
	{
		Vulture::CameraComponent& comp = m_CurrentSceneRendered->GetRegistry().get<Vulture::CameraComponent>(entity);
		if (comp.Main)
		{
			camComp = &comp;
			break;
		}
	}
	GlobalUbo ubo{};
	if (camComp == nullptr)
	{
		VL_CORE_ASSERT(false, "No main camera found!");
	}
	ubo.ProjInverse = glm::inverse(camComp->ProjMat);
	ubo.ViewInverse = glm::inverse(camComp->ViewMat);
	ubo.ViewProjectionMat = camComp->GetViewProj();
	m_GlobalDescriptorSets[Vulture::Renderer::GetCurrentFrameIndex()]->GetBuffer(0)->WriteToBuffer(&ubo);

	m_GlobalDescriptorSets[Vulture::Renderer::GetCurrentFrameIndex()]->GetBuffer(0)->Flush();
}