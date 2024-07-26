#include "pch.h"
#include "Editor.h"

#include "CameraScript.h"
#include "Components.h"

#include "Vulture.h"


void Editor::Init()
{
	m_PathTracer.Init({ (uint32_t)m_ImageSize.x, (uint32_t)m_ImageSize.y });
	m_PostProcessor.Init(m_PathTracer.GetOutputImage());
	Vulture::Renderer::SetImGuiFunction([this]() { RenderImGui(); });

	m_PathTracerOutputImageSet = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetLinearSampler().GetSamplerHandle(), m_PathTracer.GetOutputImage()->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	m_QuadPush.Init({ VK_SHADER_STAGE_VERTEX_BIT });

	CreateQuadRenderTarget();
	CreateQuadPipeline();
	CreateQuadDescriptor();

	m_Denoiser.Init();
	m_Denoiser.AllocateBuffers({ (uint32_t)m_ImageSize.x, (uint32_t)m_ImageSize.y });

	Vulture::Image::CreateInfo imageInfo{};
	imageInfo.Width = m_ImageSize.x;
	imageInfo.Height = m_ImageSize.y;
	imageInfo.Format = VK_FORMAT_R32G32B32A32_SFLOAT;
	imageInfo.Tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	imageInfo.Properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	imageInfo.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	imageInfo.LayerCount = 1;
	imageInfo.SamplerInfo = Vulture::SamplerInfo{};
	imageInfo.Type = Vulture::Image::ImageType::Image2D;
	imageInfo.DebugName = "Denoised Image";
	m_DenoisedImage.Init(imageInfo);
}

void Editor::Destroy()
{

}

void Editor::SetCurrentScene(Vulture::Scene* scene)
{
	m_CurrentScene = scene;

	m_PathTracer.SetScene(scene);
	
	// Get Vertex and index count
	auto view = m_CurrentScene->GetRegistry().view<ModelComponent>();
	for (auto& entity : view)
	{
		ModelComponent* modelComp = &m_CurrentScene->GetRegistry().get<ModelComponent>(entity); // TODO: support more than one model
		Vulture::Model* model = modelComp->ModelHandle.GetModel();
		m_VertexCount = model->GetVertexCount();
		m_IndexCount = model->GetIndexCount();
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

	if (m_ImGuiViewportResized)
	{
		m_ImGuiViewportResized = false;
		Resize();
		m_PathTracerOutputImageSet = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetLinearSampler().GetSamplerHandle(), m_QuadRenderTarget.GetImageView(0), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	if (m_ImageResized)
	{
		m_Denoiser.AllocateBuffers({ (uint32_t)m_ImageSize.x, (uint32_t)m_ImageSize.y });
		m_DenoisedImage.Resize({ (uint32_t)m_ImageSize.x, (uint32_t)m_ImageSize.y });
		m_PathTracer.Resize({ (uint32_t)m_ImageSize.x, (uint32_t)m_ImageSize.y });

		if (m_ShowDenoisedImage)
			m_PostProcessor.Resize({ (uint32_t)m_ImageSize.x, (uint32_t)m_ImageSize.y }, &m_DenoisedImage);
		else
			m_PostProcessor.Resize({ (uint32_t)m_ImageSize.x, (uint32_t)m_ImageSize.y }, m_PathTracer.GetOutputImage());

		m_ImageResized = false;
		Resize();
		m_PathTracerOutputImageSet = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetLinearSampler().GetSamplerHandle(), m_QuadRenderTarget.GetImageView(0), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	else
	{
		if (Vulture::Renderer::BeginFrame())
		{
			if (m_PathTracer.GetSamplesAccumulated() == 0) m_Time = 0.0f;
			m_PathTracingFinished = !m_PathTracer.Render();

			if (!m_PathTracingFinished)
				m_Time += m_Timer.ElapsedSeconds();

			m_PostProcessor.Evaluate();
			m_PostProcessor.Render();

			RenderViewportImage();

			Vulture::Renderer::ImGuiPass();

			m_PathTracingFinished = m_PathTracer.GetSamplesAccumulated() >= m_PathTracer.m_DrawInfo.TotalSamplesPerPixel;

			if (m_ReadyToSaveRender)
			{
				m_ReadyToSaveRender = false;
				m_FileAlreadySaved = true;
				Vulture::Renderer::SaveImageToFile("", m_PostProcessor.GetOutputImage());
			}

			// Denoiser
			// step 1:
			// First it checks whether it can upload data to cuda buffers using normal frame command buffer
			if (m_PathTracingFinished && m_RenderToFile && !m_ImageDenoised)
			{
				std::vector<Vulture::Image*> denoiserInput =
				{
					m_PathTracer.GetOutputImage(),
					m_PathTracer.GetGBuffer()->GetImageNoVk(0).get(),
					m_PathTracer.GetGBuffer()->GetImageNoVk(1).get()
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
			m_PostProcessor.EndFrame();
			
			// Denoiser
			// step 2:
			// After the first step is done it waits until all buffers are copied using WaitIdle()
			// and when that's done it runs Optix denoiser in cuda and waits untill it's done (DenoiseImageBuffer())
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

Editor::Editor()
{

}

Editor::~Editor()
{

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

	std::vector<Vulture::DescriptorSetLayout::Binding> bindings = { { 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT } };
	Vulture::DescriptorSetLayout layout(bindings);

	Vulture::Pipeline::GraphicsCreateInfo info{};
	info.AttributeDesc = Vulture::Mesh::Vertex::GetAttributeDescriptions();
	info.BindingDesc = Vulture::Mesh::Vertex::GetBindingDescriptions();
	info.BlendingEnable = false;
	info.ColorAttachmentCount = 1;
	info.PolygonMode = VK_POLYGON_MODE_FILL;
	info.CullMode = VK_CULL_MODE_NONE;
	info.debugName = "Quad Pipeline";
	info.DepthClamp = false;
	info.DepthTestEnable = false;
	info.DescriptorSetLayouts = { layout.GetDescriptorSetLayoutHandle() };
	info.Height = m_ViewportSize.height;
	info.Width = m_ViewportSize.width;
	info.PushConstants = m_QuadPush.GetRangePtr();
	info.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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

	ImGuiRenderPathTracingViewport();

	if (m_RenderToFile)
		ImGuiRenderingToFileSettings();
	else
		ImGuiPathTracerSettings();

	m_PostProcessor.RenderGraph();
}

void Editor::ImGuiRenderPathTracingViewport()
{
	Vulture::Entity cameraEntity = PerspectiveCameraComponent::GetMainCameraEntity(m_CurrentScene);

	Vulture::ScriptComponent* scComp = m_CurrentScene->GetRegistry().try_get<Vulture::ScriptComponent>(cameraEntity);
	CameraScript* camScript = scComp ? scComp->GetScript<CameraScript>(0) : nullptr;

	VL_CORE_ASSERT(camScript != nullptr, "No main camera found!");

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	m_PathTracerViewportVisible = ImGui::Begin("Path Tracer Preview Viewport");

	bool isWindowHoveredAndDocked = ImGui::IsWindowHovered() && ImGui::IsWindowDocked();

	if (m_PathTracerViewportVisible)
		camScript->m_CameraLocked = !isWindowHoveredAndDocked;

	if (m_RenderToFile)
		camScript->m_CameraLocked = true; // Always lock camera when rendering to file

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

void Editor::ImGuiPathTracerSettings()
{
	ImGui::Begin("Settings");

	ImGuiInfoHeader();
	m_Timer.Reset();

	ImGuiViewportSettings();

	ImGuiCameraSettings();

	ImGuiShaderSettings();

	ImGuiSceneEditor();

	ImGuiEnvMapSettings();

	ImGuiPathTracingSettings();

	ImGuiFileRenderSettings();

	ImGui::End();
}

void Editor::ImGuiRenderingToFileSettings()
{
	ImGui::Begin("Settings");

	ImGuiInfoHeader();
	m_Timer.Reset();

	ImGui::Separator();

	ImGui::Text("%d / %d samples accumulated", m_PathTracer.GetSamplesAccumulated(), m_PathTracer.m_DrawInfo.TotalSamplesPerPixel);

	ImGui::Separator();

	if (!m_PathTracingFinished)
	{
		if (ImGui::Button("Cancel"))
		{
			m_RenderToFile = false;
			m_PathTracer.ResetFrameAccumulation();
			m_Time = 0.0f;
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
		std::string extension = shadersStr[selectedShaderIndex].substr(shadersStr[selectedShaderIndex].find_last_of(".") + 1);
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

	std::string selectedShaderPath = shadersStr[selectedShaderIndex];

	if (ImGui::Button("Load Shader"))
	{
		if (selectedShaderType == VK_SHADER_STAGE_RAYGEN_BIT_KHR)
			m_PathTracer.m_DrawInfo.RayGenShaderPath = selectedShaderPath;
		else if (selectedShaderType == VK_SHADER_STAGE_MISS_BIT_KHR)
			m_PathTracer.m_DrawInfo.MissShaderPath = selectedShaderPath;
		else if (selectedShaderType == VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
			m_PathTracer.m_DrawInfo.HitShaderPath = selectedShaderPath;

		m_PathTracer.RecreateRayTracingPipeline();
		m_PathTracer.ResetFrameAccumulation();
	}

	std::vector<const char*> loadedShaderPaths = { m_PathTracer.m_DrawInfo.RayGenShaderPath.c_str(), m_PathTracer.m_DrawInfo.MissShaderPath.c_str(), m_PathTracer.m_DrawInfo.HitShaderPath.c_str() };

	ImGui::Separator();

	ImGui::Text("Currently Loaded Shaders");

	int selectedLoadedShaderIndex = -1;
	ImGui::PushID("Currently Loaded Shaders");
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
	ImGui::ListBox("", &selectedLoadedShaderIndex, loadedShaderPaths.data(), (int)loadedShaderPaths.size(), (int)loadedShaderPaths.size());
	ImGui::PopID();
}

void Editor::ImGuiInfoHeader()
{
	ImGui::SeparatorText("Info");

	ImGui::Text("ms %f | fps %f", m_Timer.ElapsedMillis(), 1.0f / m_Timer.ElapsedSeconds());

	ImGui::Text("Frame: %i", m_PathTracer.GetFrame()); // renderer starts counting from 0 so add 1
	ImGui::Text("Time: %fs", m_Time);
	ImGui::Text("Samples Per Pixel: %i", m_PathTracer.GetSamplesAccumulated());
	ImGui::Text("Vertices Count: %i", m_VertexCount);
	ImGui::Text("Indices Count: %i", m_IndexCount);

	if (ImGui::Button("Reset"))
	{
		m_PathTracer.ResetFrameAccumulation();
		m_Time = 0.0f;
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
		if (entry.is_regular_file())
		{
			if (entry.path().extension() == ".gltf" || entry.path().extension() == ".obj" || entry.path().extension() == ".fbx")
			{
				i++;
			}
		}
	}

	modelsStr.reserve(i);
	modelsCStr.reserve(i);
	i = 0;
	for (const auto& entry : std::filesystem::directory_iterator("assets/"))
	{
		if (entry.is_regular_file())
		{
			if (entry.path().extension() == ".gltf" || entry.path().extension() == ".obj" || entry.path().extension() == ".fbx")
			{
				modelsStr.push_back(entry.path().filename().string());
				modelsCStr.push_back(modelsStr[i].c_str());
				i++;
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

	auto view = m_CurrentScene->GetRegistry().view<ModelComponent>();
	ModelComponent* modelComp = nullptr;
	for (auto& entity : view)
	{
		modelComp = &m_CurrentScene->GetRegistry().get<ModelComponent>(entity); // TODO: support more than one model
	}

	VL_CORE_ASSERT(modelComp != nullptr, "No model in scene");


	auto currentMaterials = &modelComp->ModelHandle.GetModel()->GetMaterials();
	auto currentMeshesNames = modelComp->ModelHandle.GetModel()->GetNames();

	std::vector<const char*> meshesNames(currentMeshesNames.size());
	for (int i = 0; i < meshesNames.size(); i++)
	{
		meshesNames[i] = currentMeshesNames[i].c_str();
	}

	ImGui::ListBox("Materials", &currentMaterialItem, meshesNames.data(), (int)meshesNames.size(), meshesNames.size() > 10 ? 10 : (int)meshesNames.size());

	ImGui::SeparatorText("Material Values");

	bool valuesChanged = false;
	if (ImGui::ColorEdit3("Albedo",				(float*)&(*currentMaterials)[currentMaterialItem].Color)) { valuesChanged = true; };
	if (ImGui::ColorEdit3("Emissive Color",		(float*)&(*currentMaterials)[currentMaterialItem].EmissiveColor)) { valuesChanged = true; };
	if (ImGui::SliderFloat("Emissive Strength",	(float*)&(*currentMaterials)[currentMaterialItem].EmissiveColor.w, 0.0f, 10.0f)) { valuesChanged = true; };
	if (ImGui::SliderFloat("Roughness",			(float*)&(*currentMaterials)[currentMaterialItem].Roughness, 0.0f, 1.0f)) { valuesChanged = true; };
	if (ImGui::SliderFloat("Metallic",			(float*)&(*currentMaterials)[currentMaterialItem].Metallic, 0.0f, 1.0f)) { valuesChanged = true; };
	if (ImGui::SliderFloat("Specular Strength", (float*)&(*currentMaterials)[currentMaterialItem].SpecularStrength, 0.0f, 1.0f)) { valuesChanged = true; };
	if (ImGui::SliderFloat("Specular Tint",		(float*)&(*currentMaterials)[currentMaterialItem].SpecularTint, 0.0f, 1.0f)) { valuesChanged = true; };
	ImGui::Separator();
	
	if (ImGui::SliderFloat("Transparency",	(float*)&(*currentMaterials)[currentMaterialItem].Transparency, 0.0f, 1.0f)) { valuesChanged = true; };
	if (ImGui::SliderFloat("IOR",			(float*)&(*currentMaterials)[currentMaterialItem].Ior, 1.0f, 2.0f)) { valuesChanged = true; };
	ImGui::Separator();

	if (valuesChanged)
	{
		// Upload to GPU
		m_PathTracer.GetMaterialsBuffer()->WriteToBuffer(
			((uint8_t*)currentMaterials->data()) + (uint8_t)sizeof(Vulture::Material) * (uint8_t)currentMaterialItem,
			sizeof(Vulture::Material),
			sizeof(Vulture::Material) * currentMaterialItem
		);

		m_PathTracer.ResetFrameAccumulation();
		m_Time = 0.0f;
	}
}

void Editor::ImGuiEnvMapSettings()
{
	if (!ImGui::CollapsingHeader("Environment Map Settings"))
		return;

	ImGui::Separator();

	if (ImGui::SliderFloat("Azimuth", &m_PathTracer.m_DrawInfo.EnvAzimuth, 0.0f, 360.0f)) { m_PathTracer.ResetFrameAccumulation(); };
	if (ImGui::SliderFloat("Altitude", &m_PathTracer.m_DrawInfo.EnvAltitude, 0.0f, 360.0f)) { m_PathTracer.ResetFrameAccumulation(); };

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
				envMapsString.push_back(entry.path().filename().string());
				envMaps.push_back(envMapsString[i].c_str());
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

	if (ImGui::Checkbox("Eliminate Fireflies", &m_PathTracer.m_DrawInfo.UseFireflies))
		m_PathTracer.RecreateRayTracingPipeline();

	if (ImGui::Checkbox("Show Skybox", &m_PathTracer.m_DrawInfo.ShowSkybox))
		m_PathTracer.RecreateRayTracingPipeline();

	if (ImGui::Checkbox("Sample Environment Map", &m_PathTracer.m_DrawInfo.SampleEnvMap))
		m_PathTracer.RecreateRayTracingPipeline();

	ImGui::Text("");

	if (ImGui::SliderInt("Max Depth",			&m_PathTracer.m_DrawInfo.RayDepth, 1, 20)) { m_PathTracer.ResetFrameAccumulation(); }
	if (ImGui::SliderInt("Samples Per Pixel",	&m_PathTracer.m_DrawInfo.TotalSamplesPerPixel, 1, 50'000)) {  }
	if (ImGui::SliderInt("Samples Per Frame",	&m_PathTracer.m_DrawInfo.SamplesPerFrame, 1, 40)) {  }
	
	if (ImGui::Checkbox(   "Auto Focal Length",	&m_PathTracer.m_DrawInfo.AutoDoF)) { m_PathTracer.ResetFrameAccumulation(); }
	if (ImGui::Checkbox(   "Visualize DOF",		&m_PathTracer.m_DrawInfo.VisualizedDOF)) { m_PathTracer.ResetFrameAccumulation(); }
	if (ImGui::SliderFloat("Focal Length",		&m_PathTracer.m_DrawInfo.FocalLength, 1.0f, 100.0f)) { m_PathTracer.ResetFrameAccumulation(); }
	if (ImGui::SliderFloat("DoF Strength",		&m_PathTracer.m_DrawInfo.DOFStrength, 0.0f, 100.0f)) { m_PathTracer.ResetFrameAccumulation(); }
	if (ImGui::SliderFloat("Anti Aliasing Strength", &m_PathTracer.m_DrawInfo.AliasingJitterStr, 0.0f, 2.0f)) { m_PathTracer.ResetFrameAccumulation(); }
	ImGui::Separator();
}

void Editor::ImGuiViewportSettings()
{
	if (!ImGui::CollapsingHeader("Viewport Settings"))
		return;

	ImGui::Separator();

	if (ImGui::InputInt2("Rendered Image Size", (int*)&m_ImageSize)) { m_ImageResized = true; }

	m_ImageSize.x = glm::max(m_ImageSize.x, 1);
	m_ImageSize.y = glm::max(m_ImageSize.y, 1);

	ImGui::Separator();
}

void Editor::ImGuiCameraSettings()
{
	if (!ImGui::CollapsingHeader("Camera Settings"))
		return;

	Vulture::Entity cameraEntity = PerspectiveCameraComponent::GetMainCameraEntity(m_CurrentScene);

	Vulture::ScriptComponent* scComp = m_CurrentScene->GetRegistry().try_get<Vulture::ScriptComponent>(cameraEntity);
	PerspectiveCameraComponent* camComp = m_CurrentScene->GetRegistry().try_get<PerspectiveCameraComponent>(cameraEntity);
	CameraScript* camScript;

	if (scComp)
	{
		camScript = scComp->GetScript<CameraScript>(0);
	}

	ImGui::Separator();
	if (camScript)
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
		m_PathTracer.ResetFrameAccumulation();
		m_Time = 0.0f;
	}

	ImGui::Separator();
}

void Editor::Resize()
{
	RescaleQuad();
	CreateQuadRenderTarget();
	CreateQuadDescriptor();

	float x = m_ViewportSize.width / 2.0f;
	float y = m_ViewportSize.height / 2.0f;

	glm::vec4 size = { -x, x, -y, y };

	m_QuadCamera.SetOrthographicMatrix({ -x, x, -y, y }, 0.1f, 100.0f);
	m_QuadCamera.UpdateViewMatrix();
}

void Editor::UpdateModel()
{
	Vulture::AssetHandle newAssetHandle;

	auto view = m_CurrentScene->GetRegistry().view<ModelComponent>();
	for (auto& entity : view)
	{
		ModelComponent* modelComp = &m_CurrentScene->GetRegistry().get<ModelComponent>(entity); // TODO: support more than one model
		modelComp->ModelHandle.Unload();
		newAssetHandle = Vulture::AssetManager::LoadAsset(m_ChangedModelFilepath);
		modelComp->ModelHandle = newAssetHandle;
		newAssetHandle.WaitToLoad();

		// Get Vertex and index count
		Vulture::Model* model = newAssetHandle.GetModel();
		m_VertexCount = model->GetVertexCount();
		m_IndexCount = model->GetIndexCount();
	}

	m_PathTracer.SetScene(m_CurrentScene);
	m_PathTracer.ResetFrameAccumulation();
}

void Editor::UpdateSkybox()
{
	Vulture::AssetHandle newAssetHandle;
	auto view = m_CurrentScene->GetRegistry().view<SkyboxComponent>();
	for (auto& entity : view)
	{
		SkyboxComponent* skyboxComp = &m_CurrentScene->GetRegistry().get<SkyboxComponent>(entity); // TODO: support more than one model
		skyboxComp->ImageHandle.Unload();

		newAssetHandle = Vulture::AssetManager::LoadAsset(m_ChangedSkyboxFilepath);
		skyboxComp->ImageHandle = newAssetHandle;
		break;
	}

	newAssetHandle.WaitToLoad();
	m_PathTracer.SetScene(m_CurrentScene);
	m_PathTracer.ResetFrameAccumulation();

	m_ModelChanged = false;
}
