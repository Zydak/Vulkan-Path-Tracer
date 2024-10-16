#include "pch.h"
#include "Editor.h"

#include "CameraScript.h"
#include "Components.h"

#include "Vulture.h"


void Editor::Init()
{
	m_PathTracer.Init({ 900, 900 });
	m_PostProcessor.Init(m_PathTracer.GetOutputImage());
	Vulture::Renderer::SetImGuiFunction([this]() { RenderImGui(); });

	m_PathTracerOutputImageSet = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetLinearSampler().GetSamplerHandle(), m_PathTracer.GetOutputImage()->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	m_RasterizerOutputImageSet = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetLinearSampler().GetSamplerHandle(), m_PathTracer.GetGBufferAlbedo()->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	m_RasterizerNormalOutputImageSet = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetLinearSampler().GetSamplerHandle(), m_PathTracer.GetGBufferNormal()->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	m_QuadPush.Init({ VK_SHADER_STAGE_VERTEX_BIT });

	CreateQuadRenderTarget();
	CreateQuadPipeline();
	CreateQuadDescriptor();

	m_Denoiser.Init();
	m_Denoiser.AllocateBuffers({ 900, 900 });

	Vulture::Image::CreateInfo imageInfo{};
	imageInfo.Width = 900;
	imageInfo.Height = 900;
	imageInfo.Format = VK_FORMAT_R32G32B32A32_SFLOAT;
	imageInfo.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	imageInfo.Properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	imageInfo.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	imageInfo.SamplerInfo = Vulture::SamplerInfo{};
	imageInfo.DebugName = "Denoised Image";
	m_DenoisedImage.Init(imageInfo);
}

void Editor::Destroy()
{

}

void Editor::SetCurrentScene(Vulture::Scene** scene, Vulture::AssetHandle sceneHandle)
{
	m_CurrentScene = scene;
	m_SceneHandle = sceneHandle;

	auto viewEditor = (*m_CurrentScene)->GetRegistry().view<EditorSettingsComponent>();
	EditorSettingsComponent* editorSettings = nullptr;
	for (auto& entity : viewEditor)
	{
		VL_CORE_ASSERT(editorSettings == nullptr, "Can't have more than one tonemap settings inside a scene!");
		editorSettings = &(*m_CurrentScene)->GetRegistry().get<EditorSettingsComponent>(entity);
	}

	// No settings found, create one
	if (editorSettings == nullptr)
	{
		auto entity = (*m_CurrentScene)->CreateEntity();
		editorSettings = &entity.AddComponent<EditorSettingsComponent>();
	}

	m_PathTracer.SetScene(*scene);
	m_PostProcessor.SetScene(*scene);
	
	// Get Vertex and index count
	m_VertexCount = 0;
	m_IndexCount = 0;
	auto view = (*m_CurrentScene)->GetRegistry().view<Vulture::MeshComponent>();
	for (auto& entity : view)
	{
		Vulture::MeshComponent* meshComp = &(*m_CurrentScene)->GetRegistry().get<Vulture::MeshComponent>(entity); // TODO: support more than one model
		Vulture::Mesh* mesh = meshComp->AssetHandle.GetMesh();
		m_VertexCount += mesh->GetVertexCount();
		m_IndexCount += mesh->GetIndexCount();
 	}
}

void Editor::Render()
{
	if (m_ModelChanged)
	{
		UpdateModel();
		m_ModelChanged = false;
	}

	if (m_SkyboxChanged)
	{
		UpdateSkybox();
		m_SkyboxChanged = false;
	}

	m_PathTracer.UpdateResources();

	auto viewEditor = (*m_CurrentScene)->GetRegistry().view<EditorSettingsComponent>();
	EditorSettingsComponent* editorSettings = nullptr;
	for (auto& entity : viewEditor)
	{
		VL_CORE_ASSERT(editorSettings == nullptr, "Can't have more than one tonemap settings inside a scene!");
		editorSettings = &(*m_CurrentScene)->GetRegistry().get<EditorSettingsComponent>(entity);
	}

	// No settings found, create one
	if (editorSettings == nullptr)
	{
		auto entity = (*m_CurrentScene)->CreateEntity();
		editorSettings = &entity.AddComponent<EditorSettingsComponent>();
	}

	static VkOffset2D prevSize = { 900, 900 };
	if (prevSize.x != editorSettings->ImageSize.x || prevSize.y != editorSettings->ImageSize.y)
	{
		prevSize = editorSettings->ImageSize;
		m_ImageResized = true;
	}

	if (m_ImGuiViewportResized)
	{
		m_ImGuiViewportResized = false;
		Resize();
	}
	if (m_ImageResized)
	{
		auto viewEditor = (*m_CurrentScene)->GetRegistry().view<EditorSettingsComponent>();
		EditorSettingsComponent* editorSettings = nullptr;
		for (auto& entity : viewEditor)
		{
			VL_CORE_ASSERT(editorSettings == nullptr, "Can't have more than one tonemap settings inside a scene!");
			editorSettings = &(*m_CurrentScene)->GetRegistry().get<EditorSettingsComponent>(entity);
		}

		m_Denoiser.AllocateBuffers({ (uint32_t)editorSettings->ImageSize.x, (uint32_t)editorSettings->ImageSize.y });
		m_DenoisedImage.Resize({ (uint32_t)editorSettings->ImageSize.x, (uint32_t)editorSettings->ImageSize.y });
		m_PathTracer.Resize({ (uint32_t)editorSettings->ImageSize.x, (uint32_t)editorSettings->ImageSize.y });

		if (m_ShowDenoisedImage)
			m_PostProcessor.Resize({ (uint32_t)editorSettings->ImageSize.x, (uint32_t)editorSettings->ImageSize.y }, &m_DenoisedImage);
		else
			m_PostProcessor.Resize({ (uint32_t)editorSettings->ImageSize.x, (uint32_t)editorSettings->ImageSize.y }, m_PathTracer.GetOutputImage());

		m_ImageResized = false;
		Resize();
	}
	else
	{
		if (Vulture::Renderer::BeginFrame())
		{
			if (m_PathTracer.GetSamplesAccumulated() == 0) m_Time = 0.0f;
			m_PathTracingFinished = !m_PathTracer.Render();

			if (!m_PathTracingFinished)
				m_Time += m_Timer.ElapsedSeconds();

			m_PostProcessor.Render();

			RenderViewportImage();

			Vulture::Renderer::ImGuiPass();

			auto viewPathTracing = (*m_CurrentScene)->GetRegistry().view<PathTracingSettingsComponent>();
			PathTracingSettingsComponent* pathTracingSettings = nullptr;
			for (auto& entity : viewPathTracing)
			{
				VL_CORE_ASSERT(pathTracingSettings == nullptr, "Can't have more than one tonemap settings inside a scene!");
				pathTracingSettings = &(*m_CurrentScene)->GetRegistry().get<PathTracingSettingsComponent>(entity);
			}

			VL_CORE_ASSERT(pathTracingSettings != nullptr, "Couldn't find path tracing settings!");

			m_PathTracingFinished = m_PathTracer.GetSamplesAccumulated() >= pathTracingSettings->Settings.TotalSamplesPerPixel;

			if (m_ReadyToSaveRender)
			{
				m_ReadyToSaveRender = false;
				m_FileAlreadySaved = true;
				Vulture::Renderer::SaveImageToFile("", m_PostProcessor.GetOutputImage());
			}

			// Denoiser
			// step 1:
			// First it upload data to cuda buffers using normal frame command buffer
			if (m_PathTracingFinished && m_RenderToFile && !m_ImageDenoised)
			{
				std::vector<Vulture::Image*> denoiserInput =
				{
					m_PathTracer.GetOutputImage(),
					m_PathTracer.GetGBufferAlbedo(),
					m_PathTracer.GetGBufferNormal()
				};

				m_Denoiser.ImageToBuffer(Vulture::Renderer::GetCurrentCommandBuffer(), denoiserInput);
			}

			// step 3:
			// When m_ImageDenoised is set to true and it hasn't been copied already (m_DenoisedImageReady)
			// it copies the data from cuda buffers into m_DenoisedImage
			// 
			// This way you have to wait 2 frames for denoising, step 1 is run on the first frame,
			// step 2 is run between frames, and step 3 is run on the second frame
			if (m_PathTracingFinished && m_RenderToFile && m_ImageDenoised && !m_DenoisedImageReady)
			{
				m_Denoiser.BufferToImage(Vulture::Renderer::GetCurrentCommandBuffer(), &m_DenoisedImage);
				m_DenoisedImageReady = true;
			}

			Vulture::Renderer::EndFrame();
			
			// Denoiser
			// step 2:
			// After the first step is done it waits until all buffers are copied using WaitIdle()
			// and when that's done it runs Optix denoiser in cuda and waits until it's done (DenoiseImageBuffer())
			if (m_PathTracingFinished && m_RenderToFile && !m_ImageDenoised)
			{
				m_ImageDenoised = true;
				Vulture::Device::WaitIdle();
				uint64_t x = UINT64_MAX;
				m_Denoiser.DenoiseImageBuffer(x);
			}

		}
		else
		{
			Resize();
		}
	}
}

void Editor::CreateQuadPipeline()
{
	Vulture::Shader::CreateInfo vertexShaderInfo{};
	vertexShaderInfo.Filepath = "src/shaders/Quad.vert";
	vertexShaderInfo.Type = VK_SHADER_STAGE_VERTEX_BIT;
	Vulture::Shader vertexShader(vertexShaderInfo);

	Vulture::Shader::CreateInfo fragmentShaderInfo{};
	fragmentShaderInfo.Filepath = "src/shaders/Quad.frag";
	fragmentShaderInfo.Type = VK_SHADER_STAGE_FRAGMENT_BIT;
	Vulture::Shader fragmentShader(fragmentShaderInfo);

	std::vector<Vulture::DescriptorSetLayout::Binding> bindings = { { 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT } }; //-V826
	Vulture::DescriptorSetLayout layout(bindings);

	Vulture::Pipeline::GraphicsCreateInfo info{};
	info.AttributeDesc = Vulture::Mesh::Vertex::GetAttributeDescriptions();
	info.BindingDesc = Vulture::Mesh::Vertex::GetBindingDescriptions();
	info.debugName = "Quad Pipeline";
	info.DescriptorSetLayouts = { layout.GetDescriptorSetLayoutHandle() };
	info.Height = m_ViewportSize.height;
	info.Width = m_ViewportSize.width;
	info.PushConstants = m_QuadPush.GetRangePtr();
	info.Shaders = { &vertexShader, &fragmentShader };
	info.RenderPass = m_QuadRenderTarget.GetRenderPass();

	m_QuadPipeline.Init(info);
}

void Editor::CreateQuadRenderTarget()
{
	// Framebuffer
	{
		VkSubpassDependency dependency{};
		dependency.srcSubpass = 0;
		dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		Vulture::Framebuffer::RenderPassCreateInfo renderPassInfo{};
		renderPassInfo.FinalLayouts = { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		renderPassInfo.Dependencies = { dependency };
		Vulture::Framebuffer::CreateInfo info{};
		info.AttachmentsFormats = { Vulture::FramebufferAttachment::ColorRGBA8 };
		info.Extent = { m_ViewportSize.width, m_ViewportSize.height };
		info.RenderPassInfo = &renderPassInfo;
		m_QuadRenderTarget.Init(info);
	}
}

void Editor::RescaleQuad()
{
	VkExtent2D imageSize = m_PostProcessor.GetOutputImage()->GetImageSize();

	float imageAspectRatio = (float)imageSize.width / (float)imageSize.height;
	float viewportAspectRatio = (float)m_ViewportSize.width / (float)m_ViewportSize.height;

	float maxScale = glm::max(imageAspectRatio, viewportAspectRatio);

	float scaleX = imageAspectRatio / maxScale;
	float scaleY = viewportAspectRatio / maxScale;

	scaleX *= m_ViewportSize.width / 2.0f;
	scaleY *= m_ViewportSize.height / 2.0f;

	m_ImageQuadTransform.Init(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f), glm::vec3(scaleX, scaleY, 1.0f));
}

void Editor::CreateQuadDescriptor()
{
	std::vector<Vulture::DescriptorSetLayout::Binding> bindings = { { 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT } };

	m_QuadDescriptor.Init(&Vulture::Renderer::GetDescriptorPool(), bindings);
	m_QuadDescriptor.AddImageSampler(0, { Vulture::Renderer::GetLinearSampler().GetSamplerHandle(), m_PostProcessor.GetOutputImage()->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
	m_QuadDescriptor.Build();
}

void Editor::RenderViewportImage()
{
	glm::mat4 view = m_QuadCamera.GetProjView();
	view = glm::scale(view, glm::vec3(1.0f, -1.0f, 1.0f));
	m_QuadPush.SetData({ view * m_ImageQuadTransform.GetMat4() });

	std::vector<VkClearValue> clearColors;
	clearColors.push_back({ 0.1f, 0.1f, 0.1f, 1.0f });
	m_QuadRenderTarget.Bind(Vulture::Renderer::GetCurrentCommandBuffer(), clearColors);

	m_QuadPipeline.Bind(Vulture::Renderer::GetCurrentCommandBuffer());

	m_QuadPush.Push(m_QuadPipeline.GetPipelineLayout(), Vulture::Renderer::GetCurrentCommandBuffer());
	m_QuadDescriptor.Bind(0, m_QuadPipeline.GetPipelineLayout(), VK_PIPELINE_BIND_POINT_GRAPHICS, Vulture::Renderer::GetCurrentCommandBuffer());

	Vulture::Renderer::GetQuadMesh().Bind(Vulture::Renderer::GetCurrentCommandBuffer());
	Vulture::Renderer::GetQuadMesh().Draw(Vulture::Renderer::GetCurrentCommandBuffer(), 1, 0);

	m_QuadRenderTarget.Unbind(Vulture::Renderer::GetCurrentCommandBuffer());
}

void Editor::RenderImGui()
{
	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		ImGuiID dockspaceID = ImGui::GetID("Dockspace");
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::DockSpaceOverViewport(viewport);
	}

	ImGuiRenderRasterizerViewport();
	ImGuiRenderPathTracingViewport();

	if (m_RenderToFile)
		ImGuiRenderingToFileSettings();
	else
		ImGuiPathTracerSettings();
}

void Editor::ImGuiRenderPathTracingViewport()
{
	Vulture::Entity cameraEntity = PerspectiveCameraComponent::GetMainCameraEntity(*m_CurrentScene);

	Vulture::ScriptComponent* scComp = (*m_CurrentScene)->GetRegistry().try_get<Vulture::ScriptComponent>(cameraEntity);
	CameraScript* camScript = scComp ? scComp->GetScript<CameraScript>(0) : nullptr;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	m_PathTracerViewportVisible = ImGui::Begin("Path Tracer Preview Viewport");

	bool isWindowHoveredAndDocked = ImGui::IsWindowHovered() && ImGui::IsWindowDocked();

	if (camScript)
	{
		if (m_PathTracerViewportVisible)
			camScript->m_CameraLocked = !isWindowHoveredAndDocked;
	
		if (m_RenderToFile)
			camScript->m_CameraLocked = true; // Always lock camera when rendering to file
	}

	static ImVec2 prevViewportSize = { 0, 0 };
	ImVec2 viewportContentSize = ImGui::GetContentRegionAvail();
	uint32_t viewportWidth  = (uint32_t)viewportContentSize.x;
	uint32_t viewportHeight = (uint32_t)viewportContentSize.y;
	if (viewportWidth != prevViewportSize.x || viewportHeight != prevViewportSize.y)
	{
		m_ImGuiViewportResized = true;
		prevViewportSize = viewportContentSize;
	}

	m_ViewportSize = { viewportWidth, viewportHeight };

	ImGui::Image(m_PathTracerOutputImageSet, viewportContentSize);

	ImGui::End();
	ImGui::PopStyleVar();
}

void Editor::ImGuiRenderRasterizerViewport()
{
	Vulture::Entity cameraEntity = PerspectiveCameraComponent::GetMainCameraEntity(*m_CurrentScene);

	Vulture::ScriptComponent* scComp = (*m_CurrentScene)->GetRegistry().try_get<Vulture::ScriptComponent>(cameraEntity);
	CameraScript* camScript = scComp ? scComp->GetScript<CameraScript>(0) : nullptr;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	m_PathTracerViewportVisible = ImGui::Begin("Rasterizer Preview Viewport");

	bool isWindowHoveredAndDocked = ImGui::IsWindowHovered() && ImGui::IsWindowDocked();

	static ImVec2 prevViewportSize = { 0, 0 };
	ImVec2 viewportContentSize = ImGui::GetContentRegionAvail();
	uint32_t viewportWidth = (uint32_t)viewportContentSize.x;
	uint32_t viewportHeight = (uint32_t)viewportContentSize.y;
	if (viewportWidth != prevViewportSize.x || viewportHeight != prevViewportSize.y)
	{
		m_ImGuiViewportResized = true;
		prevViewportSize = viewportContentSize;
	}

	m_ViewportSize = { viewportWidth, viewportHeight };

	auto viewEditor = (*m_CurrentScene)->GetRegistry().view<EditorSettingsComponent>();
	EditorSettingsComponent* editorSettings = nullptr;
	for (auto& entity : viewEditor)
	{
		VL_CORE_ASSERT(editorSettings == nullptr, "Can't have more than one tonemap settings inside a scene!");
		editorSettings = &(*m_CurrentScene)->GetRegistry().get<EditorSettingsComponent>(entity);
	}

	VL_CORE_ASSERT(editorSettings != nullptr, "Can't find editor settings");

	ImVec2 size = { (float)editorSettings->ImageSize.x, (float)editorSettings->ImageSize.y };

	float s = glm::min((float)viewportWidth / (float)editorSettings->ImageSize.x, (float)viewportHeight / (float)editorSettings->ImageSize.y);
	ImGui::SameLine();
	ImGui::SetCursorScreenPos(ImVec2{ ((float)viewportWidth - ((float)editorSettings->ImageSize.x * s)) / 2.0f, ImGui::GetCursorPos().y + ((float)viewportHeight - ((float)editorSettings->ImageSize.y * s)) / 2.0f });
	
	if (m_RasterizerShowColor)
		ImGui::Image(m_RasterizerOutputImageSet, { editorSettings->ImageSize.x * s, editorSettings->ImageSize.y * s });
	else
		ImGui::Image(m_RasterizerNormalOutputImageSet, { editorSettings->ImageSize.x * s, editorSettings->ImageSize.y * s });

	ImGui::End();
	ImGui::PopStyleVar();
}

void Editor::ImGuiPathTracerSettings()
{
	ImGui::Begin("Settings");

	ImGuiInfoHeader(true);
	m_Timer.Reset();

	ImGuiViewportSettings();

	ImGuiCameraSettings();

	ImGuiShaderSettings();

	ImGuiSceneEditor();

	ImGuiEnvMapSettings();

	ImGuiPostProcessingSettings();

	ImGuiPathTracingSettings();

	ImGuiFileRenderSettings();

	ImGuiSerializationSettings();

	ImGuiRasterizerViewSettings();

	ImGui::End();
}

void Editor::ImGuiRenderingToFileSettings()
{
	ImGui::Begin("Settings");

	ImGuiInfoHeader(false);
	m_Timer.Reset();

	ImGui::Separator();

	ImGuiPostProcessingSettings();

	auto viewPathTracing = (*m_CurrentScene)->GetRegistry().view<PathTracingSettingsComponent>();
	PathTracingSettingsComponent* pathTracingSettings = nullptr;
	for (auto& entity : viewPathTracing)
	{
		VL_CORE_ASSERT(pathTracingSettings == nullptr, "Can't have more than one tonemap settings inside a scene!");
		pathTracingSettings = &(*m_CurrentScene)->GetRegistry().get<PathTracingSettingsComponent>(entity);
	}
	VL_CORE_ASSERT(pathTracingSettings != nullptr, "Couldn't find path tracing settings!");

	ImGui::Text("%d / %d samples accumulated", m_PathTracer.GetSamplesAccumulated(), pathTracingSettings->Settings.TotalSamplesPerPixel);

	ImGui::Separator();

	if (!m_PathTracingFinished)
	{
		if (ImGui::Button("Cancel"))
		{
			m_RenderToFile = false;
		}
	}
	else
	{
		ImGui::Text("File Ready To Save!");
		ImGui::Text("You Can Play With Post Processor And Save When You're Done");

		bool x = m_FileAlreadySaved;

		if (ImGui::Button("Save To File"))
		{
			m_ReadyToSaveRender = true;
			m_FileAlreadySaved = false;
		}

		if (x)
		{
			ImGui::Text("Saved To File!");
			ImGui::Text("Check Rendered_Images/ For A File");
		}

		if (ImGui::Button("Back"))
		{
			if (m_ShowDenoisedImage)
				m_PostProcessor.UpdateInputImage(m_PathTracer.GetOutputImage()); // Reset the image back to the path tracer output if it was selected to show denoised one

			m_RenderToFile = false;
			m_ImageDenoised = false;
			m_DenoisedImageReady = false;
			m_ShowDenoisedImage = false;
			m_FileAlreadySaved = false;
		}

		bool showDenoisedPrev = m_ShowDenoisedImage;
		ImGui::Checkbox("Show Denoised Image", &m_ShowDenoisedImage);

		if (showDenoisedPrev != m_ShowDenoisedImage)
		{
			if (m_ShowDenoisedImage)
				m_PostProcessor.UpdateInputImage(&m_DenoisedImage);
			else
				m_PostProcessor.UpdateInputImage(m_PathTracer.GetOutputImage());
		}
	}

	ImGui::End();
}

void Editor::ImGuiShaderSettings()
{
	if (!ImGui::CollapsingHeader("Shader Settings"))
		return;

	std::vector<std::string> shadersStr;
	std::vector<const char*> shadersCStr;

	int fileCount = 0;
	for (const auto& entry : std::filesystem::directory_iterator("src/shaders/"))
	{
		if (entry.is_regular_file() && (entry.path().extension() == ".rchit" || entry.path().extension() == ".rmiss" || entry.path().extension() == ".rgen"))
		{
			fileCount++;
		}
	}

	shadersStr.reserve(fileCount);
	shadersCStr.reserve(fileCount);

	int i = 0;
	for (const auto& entry : std::filesystem::directory_iterator("src/shaders/"))
	{
		if (entry.is_regular_file() && (entry.path().extension() == ".rchit" || entry.path().extension() == ".rmiss" || entry.path().extension() == ".rgen"))
		{
			shadersStr.emplace_back(entry.path().string());
			shadersCStr.emplace_back(shadersStr[i].c_str());
			i++;
		}
	}

	static int selectedShaderIndex = 0;
	static VkShaderStageFlagBits selectedShaderType = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
	ImGui::Text("Available Shaders");
	ImGui::PushID("Available Shaders");
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
	if (ImGui::ListBox("", &selectedShaderIndex, shadersCStr.data(), (int)shadersCStr.size(), shadersCStr.size() > 10 ? 10 : (int)shadersCStr.size()))
	{
		std::string extension = shadersStr[selectedShaderIndex].substr(shadersStr[selectedShaderIndex].find_last_of('.') + 1);
		if (extension == "rchit")
			selectedShaderType = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
		else if (extension == "rmiss")
			selectedShaderType = VK_SHADER_STAGE_MISS_BIT_KHR;
		else if (extension == "rgen")
			selectedShaderType = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	}
	ImGui::PopID();

	if (shadersStr.empty())
	{
		ImGui::Button("Load Shader");
		return;
	}

	auto viewPathTracing = (*m_CurrentScene)->GetRegistry().view<PathTracingSettingsComponent>();
	PathTracingSettingsComponent* pathTracingSettings = nullptr;
	for (auto& entity : viewPathTracing)
	{
		VL_CORE_ASSERT(pathTracingSettings == nullptr, "Can't have more than one tonemap settings inside a scene!");
		pathTracingSettings = &(*m_CurrentScene)->GetRegistry().get<PathTracingSettingsComponent>(entity);
	}

	if (ImGui::Button("Load Shader"))
	{
		if (selectedShaderType == VK_SHADER_STAGE_RAYGEN_BIT_KHR)
			pathTracingSettings->Settings.RayGenShaderPath = shadersStr[selectedShaderIndex];
		else if (selectedShaderType == VK_SHADER_STAGE_MISS_BIT_KHR)
			pathTracingSettings->Settings.MissShaderPath = shadersStr[selectedShaderIndex];
		else if (selectedShaderType == VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
			pathTracingSettings->Settings.HitShaderPath = shadersStr[selectedShaderIndex];

		m_PathTracer.RecreateRayTracingPipeline();
		m_PathTracer.ResetFrameAccumulation();
	}

	std::vector<const char*> loadedShaderPaths = { pathTracingSettings->Settings.RayGenShaderPath.c_str(), pathTracingSettings->Settings.MissShaderPath.c_str(), pathTracingSettings->Settings.HitShaderPath.c_str() };

	ImGui::Separator();

	ImGui::Text("Currently Loaded Shaders");

	int selectedLoadedShaderIndex = -1;
	ImGui::PushID("Currently Loaded Shaders");
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
	ImGui::ListBox("", &selectedLoadedShaderIndex, loadedShaderPaths.data(), (int)loadedShaderPaths.size(), (int)loadedShaderPaths.size());
	ImGui::PopID();
}

void Editor::ImGuiInfoHeader(bool resetButton)
{
	ImGui::SeparatorText("Info");

	ImGui::Text("ms %f | fps %f", m_Timer.ElapsedMillis(), 1.0f / m_Timer.ElapsedSeconds());

	ImGui::Text("Frame: %i", m_PathTracer.GetFrame()); // renderer starts counting from 0 so add 1
	ImGui::Text("Time: %fs", m_Time);
	ImGui::Text("Samples Per Pixel: %i", m_PathTracer.GetSamplesAccumulated());
	ImGui::Text("Vertices Count: %i", m_VertexCount);
	ImGui::Text("Indices Count: %i", m_IndexCount);

	if (resetButton)
	{
		if (ImGui::Button("Reset"))
		{
			m_PathTracer.ResetFrameAccumulation();
			m_Time = 0.0f;
		}
	}

	ImGui::SeparatorText("Info");
}

void Editor::ImGuiSceneEditor()
{
	if (!ImGui::CollapsingHeader("Scene Editor"))
		return;

	ImGui::SeparatorText("Current Model");

	std::vector<std::string> modelsStr;
	std::vector<const char*> modelsCStr;

	int i = 0;
	for (const auto& entry : std::filesystem::directory_iterator("assets/"))
	{
		auto& path = entry.path();
		if (entry.is_regular_file())
		{
			if (path.extension() == ".gltf" || path.extension() == ".obj" || path.extension() == ".fbx")
			{
				i++;
			}
		}
	}
	for (const auto& entry : std::filesystem::directory_iterator("assets/scenes/"))
	{
		auto& path = entry.path();
		if (entry.is_regular_file())
		{
			if (path.extension() == ".ptscene")
			{
				i++;
			}
		}
	}

	modelsStr.reserve(i);
	modelsCStr.reserve(i);
	for (const auto& entry : std::filesystem::directory_iterator("assets/"))
	{
		if (entry.is_regular_file())
		{
			auto& path = entry.path();
			if (path.extension() == ".gltf" || path.extension() == ".obj" || path.extension() == ".fbx")
			{
				modelsStr.emplace_back(path.filename().string());
				modelsCStr.emplace_back(modelsStr[modelsStr.size() - 1].c_str());
			}
		}
	}

	for (const auto& entry : std::filesystem::directory_iterator("assets/scenes/"))
	{
		if (entry.is_regular_file())
		{
			auto& path = entry.path();
			if (path.extension() == ".ptscene")
			{
				modelsStr.emplace_back("scenes/" + path.filename().string());
				modelsCStr.emplace_back(modelsStr[modelsStr.size() - 1].c_str());
			}
		}
	}

	static int currentSceneItem = 0;
	static int currentMaterialItem = 0;
	if (ImGui::ListBox("Current Scene", &currentSceneItem, modelsCStr.data(), (int)modelsCStr.size(), modelsCStr.size() > 10 ? 10 : (int)modelsCStr.size()))
	{
		m_ChangedModelFilepath = "assets/" + modelsStr[currentSceneItem];
		m_ModelChanged = true;
		currentMaterialItem = 0;
	}
	ImGui::SeparatorText("Materials");

	std::vector<std::string> materialNamesNonRepeated;
	std::vector<Vulture::Material*> materialsNonRepeated;
	std::vector<std::string> materialNamesAll;

	auto view = (*m_CurrentScene)->GetRegistry().view<Vulture::MeshComponent, Vulture::MaterialComponent>();
	for (auto& entity : view)
	{
		Vulture::MaterialComponent* materialComp = &(*m_CurrentScene)->GetRegistry().get<Vulture::MaterialComponent>(entity);
		std::string name = materialComp->AssetHandle.GetMaterial()->MaterialName;
		materialNamesAll.push_back(name);

		bool alreadyPresent = false;
		for (int i = 0; i < materialNamesNonRepeated.size(); i++)
		{
			if (materialNamesNonRepeated[i] == name)
				alreadyPresent = true;
		}

		if (alreadyPresent)
			continue;

		materialNamesNonRepeated.push_back(name);
		materialsNonRepeated.push_back(materialComp->AssetHandle.GetMaterial());
	}

	std::vector<const char*> materialNamesCstr(materialNamesNonRepeated.size());
	for (int i = 0; i < materialNamesCstr.size(); i++)
	{
		materialNamesCstr[i] = materialNamesNonRepeated[i].c_str();
	}

	ImGui::ListBox("Materials", &currentMaterialItem, materialNamesCstr.data(), (int)materialNamesCstr.size(), materialNamesCstr.size() > 10 ? 10 : (int)materialNamesCstr.size());

	ImGui::SeparatorText("Material Values");
	for (int i = 0; i < materialsNonRepeated.size(); i++)
	{
		Vulture::Material* material = materialsNonRepeated[i];

		if (material->MaterialName != materialNamesNonRepeated[currentMaterialItem])
			continue;

		Vulture::MaterialProperties* materialProps = &material->Properties;

		bool valuesChanged = false;
		if (ImGui::ColorEdit3("Albedo", (float*)&materialProps->Color)) { valuesChanged = true; };
		if (ImGui::ColorEdit3("Emissive Color", (float*)&materialProps->EmissiveColor)) { valuesChanged = true; };
		if (ImGui::SliderFloat("Emissive Strength", (float*)&materialProps->EmissiveColor.w, 0.0f, 10.0f)) { valuesChanged = true; };
		if (ImGui::SliderFloat("Roughness", (float*)&materialProps->Roughness, 0.0f, 1.0f)) { valuesChanged = true; };
		if (ImGui::SliderFloat("Metallic", (float*)&materialProps->Metallic, 0.0f, 1.0f)) { valuesChanged = true; };
		if (ImGui::SliderFloat("Anisotropy", (float*)&materialProps->Anisotropy, 0.0f, 1.0f)) { valuesChanged = true; };
		if (ImGui::SliderFloat("Anisotropy Rotation", (float*)&materialProps->AnisotropyRotation, 0.0f, 360.0f)) { valuesChanged = true; };
		if (ImGui::SliderFloat("Specular Tint", (float*)&materialProps->SpecularTint, 0.0f, 1.0f)) { valuesChanged = true; };
		ImGui::Separator();

		if (ImGui::SliderFloat("Transparency", (float*)&materialProps->Transparency, 0.0f, 1.0f)) { valuesChanged = true; };
		if (ImGui::SliderFloat("Medium Density", (float*)&materialProps->MediumDensity, 0.0f, 2.0f)) { valuesChanged = true; };
		if (ImGui::SliderFloat("Medium Anisotropy", (float*)&materialProps->MediumAnisotropy, -1.0f, 1.0f)) { valuesChanged = true; };
		if (ImGui::ColorEdit3("Medium Color", (float*)&materialProps->MediumColor)) { valuesChanged = true; };
		if (ImGui::SliderFloat("IOR", (float*)&materialProps->Ior, 1.0f, 3.0f)) { valuesChanged = true; };
		ImGui::Separator();

		if (valuesChanged)
		{
			int index = 0;
			for (auto& entity1 : view)
			{
				Vulture::MaterialComponent* materialComp = &(*m_CurrentScene)->GetRegistry().get<Vulture::MaterialComponent>(entity1);
				std::string name = materialComp->AssetHandle.GetMaterial()->MaterialName;

				if (material->MaterialName == name)
				{
					// Upload to GPU
					m_PathTracer.GetMaterialsBuffer()->WriteToBuffer(
						materialProps,
						sizeof(Vulture::MaterialProperties),
						sizeof(Vulture::MaterialProperties) * index
					);
				}
				index++;
			}

			m_PathTracer.ResetFrameAccumulation();
			m_Time = 0.0f;
		}
	}

	ImGui::SeparatorText("Volumes");

	std::vector<std::string> volumeNames;
	std::vector<const char*> volumeNamesCStr;
	std::vector<VolumeComponent> volumes;
	std::vector<VolumeComponent*> volumePtrs;
	std::vector<entt::entity> volumeEntities;

	int volumesCount = 0;
	auto volumesView = (*m_CurrentScene)->GetRegistry().view<VolumeComponent>();
	for (auto& entity : volumesView)
	{
		volumeNames.push_back("Volume" + std::to_string(volumesCount));

		auto& volumeComp = (*m_CurrentScene)->GetRegistry().get<VolumeComponent>(entity);
		volumePtrs.push_back(&volumeComp);
		volumes.push_back(volumeComp);
		volumeEntities.push_back(entity);
		
		volumesCount++;
	}

	for (int i = 0; i < volumesCount; i++)
	{
		volumeNamesCStr.push_back(volumeNames[i].c_str());
	}

	static int currentVolume = 0;
	if (volumesCount > 0)
	{
		ImGui::ListBox("Volumes", &currentVolume, volumeNamesCStr.data(), (int)volumeNamesCStr.size(), volumeNamesCStr.size() > 10 ? 10 : (int)volumeNamesCStr.size());

		bool volumeValuesChanged = false;
		if (ImGui::SliderFloat("G Anisotropy", &volumes[currentVolume].G, -0.9f, 0.9f)) { volumeValuesChanged = true; };
		if (ImGui::SliderFloat("Scattering Coefficient", &volumes[currentVolume].ScatteringCoefficient, 0.0f, 0.1f)) { volumeValuesChanged = true; };
		if (ImGui::ColorEdit3("Color", (float*)&volumes[currentVolume].Color)) { volumeValuesChanged = true; };
		if (ImGui::SliderFloat3("AABB min", (float*)&volumes[currentVolume].AABB.A, 0.0f, 100.0f)) { volumeValuesChanged = true; };
		if (ImGui::SliderFloat3("AABB max", (float*)&volumes[currentVolume].AABB.B, 0.0f, 100.0f)) { volumeValuesChanged = true; };

		if (volumeValuesChanged)
		{
			*volumePtrs[currentVolume] = volumes[currentVolume];

			m_PathTracer.GetVolumesBuffer()->WriteToBuffer(
				volumes.data(),
				sizeof(VolumeComponent) * volumesCount
			);

			m_PathTracer.ResetFrameAccumulation();
			m_Time = 0.0f;
		}
	}

	if (ImGui::Button("Add Volume"))
	{
		Vulture::Entity entity = (*m_CurrentScene)->CreateEntity();
		auto& newComp = entity.AddComponent<VolumeComponent>();
		volumes.push_back(newComp);
		volumesCount++;

		m_PathTracer.GetVolumesBuffer()->WriteToBuffer(
			volumes.data(),
			sizeof(VolumeComponent) * volumesCount
		);

		m_PathTracer.ResetFrameAccumulation();
		m_Time = 0.0f;
	}

	if (volumesCount)
	{
		if (ImGui::Button("Remove Volume"))
		{
			(*m_CurrentScene)->GetRegistry().destroy(volumeEntities[currentVolume]);
			currentVolume = 0;

			m_PathTracer.ResetFrameAccumulation();
			m_Time = 0.0f;
		}
	}

	ImGui::Separator();
}

void Editor::ImGuiEnvMapSettings()
{
	if (!ImGui::CollapsingHeader("Environment Map Settings"))
		return;

	ImGui::Separator();

	auto viewPathTracing = (*m_CurrentScene)->GetRegistry().view<PathTracingSettingsComponent>();
	PathTracingSettingsComponent* pathTracingSettings = nullptr;
	for (auto& entity : viewPathTracing)
	{
		VL_CORE_ASSERT(pathTracingSettings == nullptr, "Can't have more than one tonemap settings inside a scene!");
		pathTracingSettings = &(*m_CurrentScene)->GetRegistry().get<PathTracingSettingsComponent>(entity);
	}

	if (ImGui::SliderFloat("Azimuth", &pathTracingSettings->Settings.EnvAzimuth, 0.0f, 360.0f)) { m_PathTracer.ResetFrameAccumulation(); };
	if (ImGui::SliderFloat("Altitude", &pathTracingSettings->Settings.EnvAltitude, 0.0f, 360.0f)) { m_PathTracer.ResetFrameAccumulation(); };

	std::vector<std::string> envMapsString;
	std::vector<const char*> envMaps;

	int i = 0;
	for (const auto& entry : std::filesystem::directory_iterator("assets/"))
	{
		if (entry.is_regular_file())
		{
			if (entry.path().extension() == ".hdr")
			{
				i++;
			}
		}
	}

	envMapsString.reserve(i);
	envMaps.reserve(i);
	i = 0;
	for (const auto& entry : std::filesystem::directory_iterator("assets/"))
	{
		if (entry.is_regular_file())
		{
			if (entry.path().extension() == ".hdr")
			{
				envMapsString.emplace_back(entry.path().filename().string());
				envMaps.emplace_back(envMapsString[i].c_str());
				i++;
			}
		}
	}

	static int currentItem = 0;
	if (ImGui::ListBox("Current Environment Map", &currentItem, envMaps.data(), (int)envMaps.size(), envMaps.size() > 10 ? 10 : (int)envMaps.size()))
	{
		m_ChangedSkyboxFilepath = "assets/" + envMapsString[currentItem];
		m_SkyboxChanged = true;
	}

	ImGui::Separator();
}

void Editor::ImGuiPathTracingSettings()
{
	if (!ImGui::CollapsingHeader("Path Tracing Settings"))
		return;

	ImGui::Separator();

	auto viewPathTracing = (*m_CurrentScene)->GetRegistry().view<PathTracingSettingsComponent>();
	PathTracingSettingsComponent* pathTracingSettings = nullptr;
	for (auto& entity : viewPathTracing)
	{
		VL_CORE_ASSERT(pathTracingSettings == nullptr, "Can't have more than one tonemap settings inside a scene!");
		pathTracingSettings = &(*m_CurrentScene)->GetRegistry().get<PathTracingSettingsComponent>(entity);
	}

	if (ImGui::Checkbox("Suppress Caustics", &pathTracingSettings->Settings.UseCausticsSuppresion))
		m_PathTracer.RecreateRayTracingPipeline();

	if (ImGui::SliderFloat("Caustics Suppresion Max Luminance", &pathTracingSettings->Settings.CausticsSuppresionMaxLuminance, 1.0f, 500.0f)) { m_PathTracer.ResetFrameAccumulation(); }

	if (ImGui::Checkbox("Show Skybox", &pathTracingSettings->Settings.ShowSkybox))
		m_PathTracer.RecreateRayTracingPipeline();

	if (ImGui::Checkbox("Furnace Test Mode", &pathTracingSettings->Settings.FurnaceTestMode))
		m_PathTracer.RecreateRayTracingPipeline();

	ImGui::Text("");

	if (ImGui::SliderInt("Max Depth",			&pathTracingSettings->Settings.RayDepth, 1, 20)) { m_PathTracer.ResetFrameAccumulation(); }
	if (ImGui::SliderInt("Samples Per Pixel",	&pathTracingSettings->Settings.TotalSamplesPerPixel, 1, 50'000)) {  }
	if (ImGui::SliderInt("Samples Per Frame",	&pathTracingSettings->Settings.SamplesPerFrame, 1, 40)) {  }
	
	if (ImGui::Checkbox(   "Auto Focal Length",	&pathTracingSettings->Settings.AutoDoF)) { m_PathTracer.ResetFrameAccumulation(); }
	if (ImGui::Checkbox(   "Visualize DOF",		&pathTracingSettings->Settings.VisualizedDOF)) { m_PathTracer.ResetFrameAccumulation(); }
	if (ImGui::SliderFloat("Focal Length",		&pathTracingSettings->Settings.FocalLength, 1.0f, 100.0f)) { m_PathTracer.ResetFrameAccumulation(); }
	if (ImGui::SliderFloat("DoF Strength",		&pathTracingSettings->Settings.DOFStrength, 0.0f, 100.0f)) { m_PathTracer.ResetFrameAccumulation(); }
	if (ImGui::SliderFloat("Anti Aliasing Strength", &pathTracingSettings->Settings.AliasingJitterStr, 0.0f, 2.0f)) { m_PathTracer.ResetFrameAccumulation(); }
	ImGui::Separator();
}

void Editor::ImGuiPostProcessingSettings()
{
	if (!ImGui::CollapsingHeader("Post Processing Settings"))
		return;
	ImGui::Separator();

	auto viewTonemap = (*m_CurrentScene)->GetRegistry().view<Vulture::TonemapperSettingsComponent>();
	Vulture::TonemapperSettingsComponent* tonemapSettings = nullptr;
	for (auto& entity : viewTonemap)
	{
		VL_CORE_ASSERT(tonemapSettings == nullptr, "Can't have more than one tonemap settings inside a scene!");
		tonemapSettings = &(*m_CurrentScene)->GetRegistry().get<Vulture::TonemapperSettingsComponent>(entity);
	}

	VL_CORE_ASSERT(tonemapSettings != nullptr, "Couldn't find post processor settings!");

	ImGui::SliderFloat("Exposure", &tonemapSettings->Settings.Exposure, 0.0f, 2.0f);
	ImGui::SliderFloat("Contrast", &tonemapSettings->Settings.Contrast, 0.0f, 2.0f);
	ImGui::SliderFloat("Saturation", &tonemapSettings->Settings.Saturation, 0.0f, 2.0f);
	ImGui::SliderFloat("Brightness", &tonemapSettings->Settings.Brightness, 0.0f, 0.1f);
	ImGui::SliderFloat("Vignette", &tonemapSettings->Settings.Vignette, 0.0f, 1.0f);
	ImGui::SliderFloat("Gamma", &tonemapSettings->Settings.Gamma, 0.0f, 2.0f);
	ImGui::SliderFloat("Temperature", &tonemapSettings->Settings.Temperature, -1.0f, 1.0f);
	ImGui::SliderFloat("Tint", &tonemapSettings->Settings.Tint, -1.0f, 1.0f);

	ImGui::ColorEdit3("ColorFilter", (float*)&tonemapSettings->Settings.ColorFilter);

	ImGui::Checkbox("Chromatic Aberration", &tonemapSettings->Settings.ChromaticAberration);

	if (tonemapSettings->Settings.ChromaticAberration)
	{
		ImGui::SliderFloat("Aberration Vignette", &tonemapSettings->Settings.AberrationVignette, 0.0f, 10.0f);

		ImGui::SliderFloat2("Offset XY R", (float*)&tonemapSettings->Settings.AberrationOffsets[0], -3.0f, 3.0f);
		ImGui::SliderFloat2("Offset XY G", (float*)&tonemapSettings->Settings.AberrationOffsets[1], -3.0f, 3.0f);
		ImGui::SliderFloat2("Offset XY B", (float*)&tonemapSettings->Settings.AberrationOffsets[2], -3.0f, 3.0f);
	}
	
	const char* tonemappers[] = { "Filmic", "Hill Aces", "Narkowicz Aces", "Exposure Mapping", "Uncharted 2", "Reinchard Extended" };

	ImGui::Text("Tonemappers");
	static int currentTonemapper = 0;
	if (ImGui::ListBox("##Tonemappers", &currentTonemapper, tonemappers, IM_ARRAYSIZE(tonemappers), IM_ARRAYSIZE(tonemappers)))
	{
		tonemapSettings->Settings.Tonemapper = (Vulture::Tonemap::Tonemappers)currentTonemapper;
	}

	if (currentTonemapper == Vulture::Tonemap::Tonemappers::ReinchardExtended)
	{
		ImGui::SliderFloat("White Point", &tonemapSettings->Settings.whitePointReinhard, 0.0f, 5.0f);
	}

	ImGui::Separator();

	auto viewBloom = (*m_CurrentScene)->GetRegistry().view<Vulture::BloomSettingsComponent>();
	Vulture::BloomSettingsComponent* bloomSettings = nullptr;
	for (auto& entity : viewBloom)
	{
		VL_CORE_ASSERT(bloomSettings == nullptr, "Can't have more than one bloom settings inside a scene!");
		bloomSettings = &(*m_CurrentScene)->GetRegistry().get<Vulture::BloomSettingsComponent>(entity);
	}
	VL_CORE_ASSERT(bloomSettings != nullptr, "Couldn't find post bloom settings!");

	ImGui::SliderFloat("Threshold", &bloomSettings->Settings.Threshold, 0.0f, 3.0f);
	ImGui::SliderFloat("Strength", &bloomSettings->Settings.Strength, 0.0f, 2.0f);
	static int mipCount = 10;
	ImGui::SliderInt("Mips Count", &mipCount, 1, 10);
	bloomSettings->Settings.MipCount = (uint32_t)mipCount;

	ImGui::Separator();
}

void Editor::ImGuiViewportSettings()
{
	if (!ImGui::CollapsingHeader("Viewport Settings"))
		return;

	ImGui::Separator();

	auto viewEditor = (*m_CurrentScene)->GetRegistry().view<EditorSettingsComponent>();
	EditorSettingsComponent* editorSettings = nullptr;
	for (auto& entity : viewEditor)
	{
		VL_CORE_ASSERT(editorSettings == nullptr, "Can't have more than one tonemap settings inside a scene!");
		editorSettings = &(*m_CurrentScene)->GetRegistry().get<EditorSettingsComponent>(entity);
	}
	VL_CORE_ASSERT(editorSettings != nullptr, "Couldn't find editor settings!");

	if (ImGui::InputInt2("Rendered Image Size", (int*)&editorSettings->ImageSize)) { m_ImageResized = true; }

	editorSettings->ImageSize.x = glm::max(editorSettings->ImageSize.x, 1);
	editorSettings->ImageSize.y = glm::max(editorSettings->ImageSize.y, 1);

	ImGui::Separator();
}

void Editor::ImGuiCameraSettings()
{
	if (!ImGui::CollapsingHeader("Camera Settings"))
		return;

	Vulture::Entity cameraEntity = PerspectiveCameraComponent::GetMainCameraEntity((*m_CurrentScene));

	Vulture::ScriptComponent* scComp = (*m_CurrentScene)->GetRegistry().try_get<Vulture::ScriptComponent>(cameraEntity);
	PerspectiveCameraComponent* camComp = (*m_CurrentScene)->GetRegistry().try_get<PerspectiveCameraComponent>(cameraEntity);
	CameraScript* camScript = nullptr;

	if (scComp)
	{
		camScript = scComp->GetScript<CameraScript>(0);
	}

	ImGui::Separator();
	if (camScript != nullptr)
	{
		if (ImGui::Button("Reset Camera"))
		{
			camScript->Reset();
			camComp->Camera.UpdateViewMatrix();
		}
		ImGui::SliderFloat("Movement Speed", &camScript->m_MovementSpeed, 0.0f, 20.0f);
		ImGui::SliderFloat("Rotation Speed", &camScript->m_RotationSpeed, 0.0f, 40.0f);

		if (ImGui::SliderFloat("FOV", &camComp->Camera.FOV, 10.0f, 45.0f))
		{
			camComp->Camera.UpdateProjMatrix();
		}

		glm::vec3 position = camComp->Camera.Translation;
		glm::vec3 rotation = camComp->Camera.Rotation.GetAngles();
		bool changed = false;
		if (ImGui::InputFloat3("Position", (float*)&position)) { changed = true; };
		if (ImGui::InputFloat3("Rotation", (float*)&rotation)) { changed = true; };

		if (changed)
		{
			camComp->Camera.Translation = position;
			camComp->Camera.Rotation.SetAngles(rotation);

			camComp->Camera.UpdateViewMatrix();
		}
	}
	ImGui::Separator();
}

void Editor::ImGuiFileRenderSettings()
{
	if (!ImGui::CollapsingHeader("File Render"))
		return;

	ImGui::Separator();

	if (ImGui::Button("Render To File"))
	{
		m_RenderToFile = true;
	}

	ImGui::Separator();
}

void Editor::ImGuiSerializationSettings()
{
	if (!ImGui::CollapsingHeader("Serialization Settings"))
		return;

	ImGui::Separator();

	static std::array<char, 50> sceneName;
	static bool empty = false;
	if (!empty)
	{
		sceneName.fill('\0');
		empty = true;
	}
	
	ImGui::InputText("Scene Name", sceneName.data(), 50);

	std::string sceneNameStr;
	int index = 0;
	while (true)
	{
		char ch = sceneName[index];
		index++;

		if (ch == '\0')
			break;
		sceneNameStr.push_back(ch);
	}

	if (ImGui::Button("Serialize"))
	{
		if (std::filesystem::exists("assets/scenes/" + sceneNameStr + ".ptscene"))
		{
			ImGui::OpenPopup("Overwrite file?");
		}
		else
		{
			Vulture::Serializer::SerializeScene<
				PerspectiveCameraComponent,
				OrthographicCameraComponent,
				SkyboxComponent,
				PathTracingSettingsComponent,
				EditorSettingsComponent,
				VolumeComponent,
				Vulture::ScriptComponent,
				Vulture::MeshComponent,
				Vulture::MaterialComponent,
				Vulture::NameComponent,
				Vulture::TransformComponent,
				Vulture::TonemapperSettingsComponent,
				Vulture::BloomSettingsComponent
			>(*m_CurrentScene, "assets/scenes/" + sceneNameStr + ".ptscene");
		}
	}

	if (ImGui::BeginPopupModal("Overwrite file?"))
	{
		ImGui::Text("There already exist file with the same name.\nAre you sure you want to overwrite it?");
		if (ImGui::Button("Yes"))
		{
			Vulture::Serializer::SerializeScene<
				PerspectiveCameraComponent,
				OrthographicCameraComponent,
				SkyboxComponent,
				PathTracingSettingsComponent,
				EditorSettingsComponent,
				VolumeComponent,
				Vulture::ScriptComponent,
				Vulture::MeshComponent,
				Vulture::MaterialComponent,
				Vulture::NameComponent,
				Vulture::TransformComponent,
				Vulture::TonemapperSettingsComponent,
				Vulture::BloomSettingsComponent
			>(*m_CurrentScene, "assets/scenes/" + sceneNameStr + ".ptscene");
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("No"))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	ImGui::Separator();
}

void Editor::ImGuiRasterizerViewSettings()
{
	if (!ImGui::CollapsingHeader("Debug Settings"))
		return;

	ImGui::Checkbox("Show Color Or Normal", &m_RasterizerShowColor);
}

void Editor::Resize()
{
	Vulture::Device::WaitIdle();

	RescaleQuad();
	CreateQuadRenderTarget();
	CreateQuadDescriptor();

	float x = m_ViewportSize.width / 2.0f;
	float y = m_ViewportSize.height / 2.0f;

	glm::vec4 size = { -x, x, -y, y };

	m_QuadCamera.SetOrthographicMatrix({ -x, x, -y, y }, 0.1f, 100.0f);
	m_QuadCamera.UpdateViewMatrix();

	m_PathTracerOutputImageSet = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetLinearSampler().GetSamplerHandle(), m_QuadRenderTarget.GetImageView(0), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	m_RasterizerOutputImageSet = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetLinearSampler().GetSamplerHandle(), m_PathTracer.GetGBufferAlbedo()->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	m_RasterizerNormalOutputImageSet = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetLinearSampler().GetSamplerHandle(), m_PathTracer.GetGBufferNormal()->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Editor::UpdateModel()
{
	Vulture::Device::WaitIdle();

	Vulture::AssetHandle newAssetHandle;

	// Unload current scene
	auto view = (*m_CurrentScene)->GetRegistry().view<Vulture::MeshComponent, Vulture::MaterialComponent>();
	for (auto& entity : view)
	{
		auto [meshComp, materialComp] = (*m_CurrentScene)->GetRegistry().get<Vulture::MeshComponent, Vulture::MaterialComponent>(entity);
		
		// Unload everything
		meshComp.AssetHandle.Unload();
		if (materialComp.AssetHandle.DoesHandleExist())
			materialComp.AssetHandle.Unload();

		// Delete the entity
		(*m_CurrentScene)->GetRegistry().destroy(entity);
	}
	Vulture::DeleteQueue::ClearQueue();

	std::string extension = m_ChangedModelFilepath.substr(m_ChangedModelFilepath.find_last_of('.'));

	if (extension == ".ptscene")
	{
		auto viewTonemap = (*m_CurrentScene)->GetRegistry().view<Vulture::TonemapperSettingsComponent>();
		for (auto& entity : viewTonemap)
		{
			(*m_CurrentScene)->GetRegistry().destroy(entity);
		}

		auto viewBloom = (*m_CurrentScene)->GetRegistry().view<Vulture::BloomSettingsComponent>();
		for (auto& entity : viewBloom)
		{
			(*m_CurrentScene)->GetRegistry().destroy(entity);
		}

		auto viewPathTracing = (*m_CurrentScene)->GetRegistry().view<PathTracingSettingsComponent>();
		for (auto& entity : viewPathTracing)
		{
			(*m_CurrentScene)->GetRegistry().destroy(entity);
		}

		auto viewEditor = (*m_CurrentScene)->GetRegistry().view<EditorSettingsComponent>();
		for (auto& entity : viewEditor)
		{
			(*m_CurrentScene)->GetRegistry().destroy(entity);
		}

		// Reload entire scene asset
		m_SceneHandle.Unload();

		m_SceneHandle = Vulture::AssetManager::LoadSceneAsset<
			PerspectiveCameraComponent,
			OrthographicCameraComponent,
			SkyboxComponent,
			PathTracingSettingsComponent,
			EditorSettingsComponent,
			VolumeComponent,
			Vulture::ScriptComponent,
			Vulture::MeshComponent,
			Vulture::MaterialComponent,
			Vulture::NameComponent,
			Vulture::TransformComponent,
			Vulture::TonemapperSettingsComponent,
			Vulture::BloomSettingsComponent
		>(m_ChangedModelFilepath);

		m_SceneHandle.WaitToLoad();
		*m_CurrentScene = m_SceneHandle.GetScene();

		// Wait for every component to load
		auto view = (*m_CurrentScene)->GetRegistry().view<Vulture::MeshComponent>();
		for (auto& entity : view)
		{
			Vulture::MeshComponent* meshComp = &(*m_CurrentScene)->GetRegistry().get<Vulture::MeshComponent>(entity);
			meshComp->AssetHandle.WaitToLoad();
		}

		auto view1 = (*m_CurrentScene)->GetRegistry().view<Vulture::MaterialComponent>();
		for (auto& entity : view1)
		{
			Vulture::MaterialComponent* matComp = &(*m_CurrentScene)->GetRegistry().get<Vulture::MaterialComponent>(entity);
			Vulture::Material* mat = matComp->AssetHandle.GetMaterial();
			mat->Textures.CreateSet();
		}

		auto view2 = (*m_CurrentScene)->GetRegistry().view<SkyboxComponent>();
		for (auto& entity : view2)
		{
			SkyboxComponent* skyComp = &(*m_CurrentScene)->GetRegistry().get<SkyboxComponent>(entity);
			skyComp->ImageHandle.WaitToLoad();
		}

		(*m_CurrentScene)->InitScripts();
		(*m_CurrentScene)->InitSystems();
	}
	else
	{
		// Reload only mesh components so that camera and rest of the components are unaffected

		// Load new one
		Vulture::AssetHandle modelAssetHandle = Vulture::AssetManager::LoadAsset(m_ChangedModelFilepath);
		modelAssetHandle.WaitToLoad();

		Vulture::ModelAsset* modelAsset = (Vulture::ModelAsset*)modelAssetHandle.GetAsset();
		modelAsset->CreateEntities((*m_CurrentScene));
		modelAssetHandle.Unload(); // Unload the model asset since it's only references to mesh data
	}

	m_PathTracer.SetScene((*m_CurrentScene));
	m_PostProcessor.SetScene(*m_CurrentScene);
	m_PathTracer.ResetFrameAccumulation();

	// Get index and vertex count
	m_VertexCount = 0;
	m_IndexCount = 0;
	auto viewMesh = (*m_CurrentScene)->GetRegistry().view<Vulture::MeshComponent>();
	for (auto& entity : viewMesh)
	{
		Vulture::MeshComponent* meshComp = &(*m_CurrentScene)->GetRegistry().get<Vulture::MeshComponent>(entity); // TODO: support more than one model
		Vulture::Mesh* mesh = meshComp->AssetHandle.GetMesh();
		m_VertexCount += mesh->GetVertexCount();
		m_IndexCount += mesh->GetIndexCount();
	}
}

void Editor::UpdateSkybox()
{
	Vulture::Device::WaitIdle();

	Vulture::AssetHandle newAssetHandle;
	auto view = (*m_CurrentScene)->GetRegistry().view<SkyboxComponent>();
	for (auto& entity : view)
	{
		SkyboxComponent* skyboxComp = &(*m_CurrentScene)->GetRegistry().get<SkyboxComponent>(entity); // TODO: support more than one model
		skyboxComp->ImageHandle.Unload();

		newAssetHandle = Vulture::AssetManager::LoadAsset(m_ChangedSkyboxFilepath);
		skyboxComp->ImageHandle = newAssetHandle;
		break;
	}

	newAssetHandle.WaitToLoad();
	m_PathTracer.SetScene((*m_CurrentScene));
	m_PathTracer.ResetFrameAccumulation();

	m_ModelChanged = false;
}
