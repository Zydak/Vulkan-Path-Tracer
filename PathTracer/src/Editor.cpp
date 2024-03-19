#include "pch.h"
#include "Editor.h"

#include "State.h"
#include "CameraScript.h"

#include "Vulture.h"


void Editor::Init()
{
	m_SceneRenderer = std::make_unique<SceneRenderer>();

	Vulture::Renderer::RenderImGui([this]() { RenderImGui(); });
}

void Editor::Destroy()
{

}

void Editor::SetCurrentScene(Vulture::Scene* scene)
{
	m_CurrentScene = scene;

	m_SceneRenderer->SetCurrentScene(scene);
	m_SceneRenderer->CreateRayTracingDescriptorSets();
}

void Editor::Render()
{
	auto view = m_CurrentScene->GetRegistry().view<Vulture::ModelComponent>();
	for (auto entity : view)
	{
		State::CurrentModelEntity = { entity, m_CurrentScene };
	}

	m_SceneRenderer->UpdateResources();

	if (m_RecreateRendererResources)
	{
		m_SceneRenderer->RecreateResources();
		m_RecreateRendererResources = false;
	}

	// If we're preparing render for a file export don't apply post process effects and ignore viewport aspect ratio
	if (State::CurrentRenderState == State::RenderState::FileRender || State::CurrentRenderState == State::RenderState::FileRenderFinished)
	{
		if (Vulture::Renderer::BeginFrame())
		{
			bool rayTracingFinished = !m_SceneRenderer->RayTrace(glm::vec4(0.1f));
			if (rayTracingFinished)
			{
				State::CurrentRenderState = State::RenderState::FileRenderFinished;

				if (!m_DrawFileInfo.Denoised)
				{
					m_SceneRenderer->Denoise();

					m_DrawFileInfo.Denoised = true;
				}

				m_SceneRenderer->PostProcess();

				if (m_DrawFileInfo.SaveToFile)
				{
					vkDeviceWaitIdle(Vulture::Device::GetDevice());
					Vulture::Renderer::SaveImageToFile("", m_SceneRenderer->GetTonemappedImage());

					m_SceneRenderer->GetTonemappedImage()->TransitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
					m_DrawFileInfo.SaveToFile = false;
				}
			}
			else
			{
				m_SceneRenderer->GetPathTraceImage()->TransitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, Vulture::Renderer::GetCurrentCommandBuffer());
			}

			Vulture::Renderer::ImGuiPass();

			if (!rayTracingFinished)
			{
				m_SceneRenderer->GetPathTraceImage()->TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL, Vulture::Renderer::GetCurrentCommandBuffer());
			}

			if (!Vulture::Renderer::EndFrame())
				m_SceneRenderer->RecreateResources();
		}
		else
		{
			m_SceneRenderer->RecreateResources();
		}
	}
	else if (m_ImGuiViewportResized)
	{
		m_SceneRenderer->RecreateResources();
		m_ImGuiViewportResized = false;
	}
	else if (Vulture::Renderer::BeginFrame())
	{
		m_SceneRenderer->RayTrace(glm::vec4(0.1f));

		// Run denoised if requested
		if (State::RunDenoising)
		{
			m_SceneRenderer->Denoise();
			State::RunDenoising = false;
			State::Denoised = true;
		}

		m_SceneRenderer->PostProcess();

		Vulture::Renderer::ImGuiPass();

		if (!Vulture::Renderer::EndFrame())
			m_SceneRenderer->RecreateResources();
	}
	else
	{
		m_SceneRenderer->RecreateResources();
	}
}

Editor::Editor()
{

}

Editor::~Editor()
{
	m_SceneRenderer.reset();
}

void Editor::RenderImGui()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		ImGuiID dockspaceID = ImGui::GetID("Dockspace");
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::DockSpaceOverViewport(viewport);
	}

	ImGuiRenderViewport();

	ImGui::Begin("Settings");

	ImGuiInfoHeader();
	if (m_SceneRenderer->GetAccumulatedSamples() < m_SceneRenderer->GetDrawInfo().TotalSamplesPerPixel)
		m_Time += m_Timer.ElapsedSeconds();
	m_Timer.Reset();
	if (m_SceneRenderer->GetRTPush().GetDataPtr()->frame <= 0) { m_Time = 0; }

	if (State::CurrentRenderState == State::RenderState::PreviewRender)
	{
		if (ImGui::CollapsingHeader("Viewport"))
		{
			ImGui::Separator();
			ImGui::Text("ImGui doesn't allow to change size in dockspace\nso you have to undock the window first");
			static int size[2] = { (int)m_SceneRenderer->GetViewportSize().width, (int)m_SceneRenderer->GetViewportSize().height };
			ImGui::InputInt2("Viewport Size", (int*)&size);
			m_SceneRenderer->SetViewportSize({ (uint32_t)size[0], (uint32_t)size[1] });
			ImGui::Separator();
		}

		if (ImGui::CollapsingHeader("Scene"))
		{
			ImGuiSceneSettings();
		}

		if (ImGui::CollapsingHeader("Camera"))
		{
			ImGuiCameraSettings();
		}

		if (ImGui::CollapsingHeader("Environment Map"))
		{
			ImGuiEnvironmentMapSettings();
		}

		if (ImGui::CollapsingHeader("Post Processing"))
		{
			ImGuiPostProcessingSettings();
		}

		if (ImGui::CollapsingHeader("Path tracing"))
		{
			ImGuiPathTracingSettings();
		}

		if (ImGui::CollapsingHeader("Denoiser"))
		{
			ImGuiDenoiserSettings();
		}

		if (ImGui::CollapsingHeader("File"))
		{
			ImGuiFileSettings();
		}

		if (ImGui::CollapsingHeader("GBuffer"))
		{
			ImGuiShowGBuffer();
		}
	}
	else
	{
		Vulture::Entity cameraEntity;
		m_CurrentScene->GetMainCamera(&cameraEntity);

		Vulture::ScriptComponent* scComp = m_CurrentScene->GetRegistry().try_get<Vulture::ScriptComponent>(cameraEntity);
		Vulture::CameraComponent* camComp = m_CurrentScene->GetRegistry().try_get<Vulture::CameraComponent>(cameraEntity);
		CameraScript* camScript;

		if (scComp)
		{
			camScript = scComp->GetScript<CameraScript>(0);
		}

		if (State::CurrentRenderState == State::RenderState::FileRenderFinished)
		{
			ImGui::Separator();
			ImGui::Text("Rendering Finished!");

			if (ImGui::CollapsingHeader("Viewport"))
			{
				ImGui::Separator();
				ImGui::Text("ImGui doesn't allow to change size in dockspace\nso you have to undock the window first");
				static int size[2] = { (int)m_SceneRenderer->GetViewportSize().width, (int)m_SceneRenderer->GetViewportSize().height };
				ImGui::InputInt2("Viewport Size", (int*)&size);
				m_SceneRenderer->SetViewportSize({ (uint32_t)size[0], (uint32_t)size[1] });
				ImGui::Separator();
			}

			if (ImGui::CollapsingHeader("Post Processing"))
			{
				ImGuiPostProcessingSettings();
			}

			ImGui::Separator();
			ImGui::Checkbox("Show Denoised Image", &State::ShowDenoised);

			if (ImGui::Button("Save to file"))
			{
				m_DrawFileInfo.SaveToFile = true;
			}

			ImGui::Separator();
			if (ImGui::Button("Go Back"))
			{
				State::CurrentRenderState = State::RenderState::PreviewRender;

				camScript->m_CameraLocked = true; // lock the camera
				m_DrawFileInfo.Denoised = false; // reset denoised flag

				m_SceneRenderer->SetViewportContentSize({ (uint32_t)1, (uint32_t)1 }); // set to 1 so that it will get automatically resized on the next frame
				m_SceneRenderer->ResetFrameAccumulation();
				m_RecreateRendererResources = true;
			}
			ImGui::Separator();
		}
		else
		{
			if (ImGui::Button("Cancel"))
			{
				State::CurrentRenderState = State::RenderState::PreviewRender;

				camScript->m_CameraLocked = true; // lock the camera
				m_DrawFileInfo.Denoised = false; // reset denoised flag

				m_SceneRenderer->SetViewportContentSize({ (uint32_t)1, (uint32_t)1 }); // set to 1 so that it will get automatically resized on the next frame
				m_SceneRenderer->ResetFrameAccumulation();
				m_RecreateRendererResources = true;
			}
		}
	}

	ImGui::End();

	ImGui::Render();
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), Vulture::Renderer::GetCurrentCommandBuffer());
}

void Editor::ImGuiRenderViewport()
{
	Vulture::Entity cameraEntity;
	m_CurrentScene->GetMainCamera(&cameraEntity);

	Vulture::ScriptComponent* scComp = m_CurrentScene->GetRegistry().try_get<Vulture::ScriptComponent>(cameraEntity);
	Vulture::CameraComponent* camComp = m_CurrentScene->GetRegistry().try_get<Vulture::CameraComponent>(cameraEntity);
	CameraScript* camScript;

	if (scComp)
	{
		camScript = scComp->GetScript<CameraScript>(0);
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowSize({ (float)m_SceneRenderer->GetViewportSize().width, (float)m_SceneRenderer->GetViewportSize().height });
	ImGui::Begin("Preview Viewport");

	if (State::CurrentRenderState == State::RenderState::PreviewRender)
	{
		if (ImGui::IsWindowHovered() && (ImGui::IsWindowDocked()))
			camScript->m_CameraLocked = false;
		else
			camScript->m_CameraLocked = true;
	}

	ImVec2 viewportContentSize = ImGui::GetContentRegionAvail();

	// Show raw path tracing image if it's being rendered to a file
	if (State::CurrentRenderState == State::RenderState::FileRender)
		ImGui::Image(m_SceneRenderer->GetPathTraceDescriptor(), viewportContentSize);
	else
	{
		ImGui::Image(m_SceneRenderer->GetTonemappedDescriptor(), viewportContentSize);
	}

	VkExtent2D prevViewportContentSize = m_SceneRenderer->GetViewportContentSize();
	VkExtent2D imGuiViewportContentSize = { (unsigned int)viewportContentSize.x, (unsigned int)viewportContentSize.y };
	if (imGuiViewportContentSize.width != prevViewportContentSize.width || imGuiViewportContentSize.height != prevViewportContentSize.height)
	{
		m_ImGuiViewportResized = true;
		prevViewportContentSize = imGuiViewportContentSize;
	}
	if (State::CurrentRenderState == State::RenderState::PreviewRender)
		m_SceneRenderer->SetViewportContentSize(imGuiViewportContentSize);

	m_SceneRenderer->SetViewportSize({ (uint32_t)ImGui::GetWindowSize().x, (uint32_t)ImGui::GetWindowSize().y });

	VkExtent2D test = m_SceneRenderer->GetViewportContentSize();

	ImGui::End();
	ImGui::PopStyleVar();
}

void Editor::ImGuiInfoHeader()
{
	ImGui::SeparatorText("Info");

	ImGui::Text("ms %f | fps %f", m_Timer.ElapsedMillis(), 1.0f / m_Timer.ElapsedSeconds());
	ImGui::Text("Total vertices: %i", m_CurrentScene->GetVertexCount());
	ImGui::Text("Total indices: %i", m_CurrentScene->GetIndexCount());

	ImGui::Text("Frame: %i", m_SceneRenderer->GetRTPush().GetDataPtr()->frame + 1); // renderer starts counting from 0 so add 1
	ImGui::Text("Time: %fs", m_Time);
	ImGui::Text("Samples Per Pixel: %i", m_SceneRenderer->GetAccumulatedSamples());

	if (ImGui::Button("Reset"))
	{
		m_SceneRenderer->ResetFrameAccumulation();
		m_Time = 0.0f;
	}

	ImGui::SeparatorText("Info");
}

void Editor::ImGuiSceneSettings()
{
	ImGui::InputFloat("Models Scale", &State::ModelScale);
	ImGui::SeparatorText("Current Model");

	std::vector<std::string> scenesString;
	std::vector<const char*> scenes;

	int i = 0;
	for (const auto& entry : std::filesystem::directory_iterator("assets/"))
	{
		if (entry.is_regular_file())
		{
			if (entry.path().extension() == ".gltf" || entry.path().extension() == ".obj")
			{
				i++;
			}
		}
	}

	scenesString.reserve(i);
	scenes.reserve(i);
	i = 0;
	for (const auto& entry : std::filesystem::directory_iterator("assets/"))
	{
		if (entry.is_regular_file())
		{
			if (entry.path().extension() == ".gltf" || entry.path().extension() == ".obj")
			{
				scenesString.push_back(entry.path().filename().string());
				scenes.push_back(scenesString[i].c_str());
				i++;
			}
		}
	}

	static int currentSceneItem = 0;
	static int currentMaterialItem = 0;
	if (ImGui::ListBox("Current Scene", &currentSceneItem, scenes.data(), (int)scenes.size(), scenes.size() > 10 ? 10 : (int)scenes.size()))
	{
		State::ModelPath = "assets/" + scenesString[currentSceneItem];
		State::ModelChanged = true;
		currentMaterialItem = 0;
	}

	ImGui::SeparatorText("Materials");

	Vulture::ModelComponent comp = State::CurrentModelEntity.GetComponent<Vulture::ModelComponent>();
	 m_CurrentMaterials = &comp.Model->GetMaterials();
	 m_CurrentMeshesNames = comp.Model->GetNames();

	std::vector<const char*> meshesNames(m_CurrentMeshesNames.size());
	for (int i = 0; i < meshesNames.size(); i++)
	{
		meshesNames[i] = m_CurrentMeshesNames[i].c_str();
	}

	ImGui::ListBox("Materials", &currentMaterialItem, meshesNames.data(), (int)meshesNames.size(), meshesNames.size() > 10 ? 10 : (int)meshesNames.size());

	ImGui::SeparatorText("Material Values");

	bool valuesChanged = false;
	if (ImGui::ColorEdit4("Albedo",					(float*)&(*m_CurrentMaterials)[currentMaterialItem].Color)) { valuesChanged = true; };
	if (ImGui::ColorEdit3("Emissive",				(float*)&(*m_CurrentMaterials)[currentMaterialItem].Emissive)) { valuesChanged = true; };
	if (ImGui::SliderFloat("Emissive Strength",		(float*)&(*m_CurrentMaterials)[currentMaterialItem].Emissive.a, 0.0f, 10.0f)) { valuesChanged = true; };
	if (ImGui::SliderFloat("Roughness",				(float*)&(*m_CurrentMaterials)[currentMaterialItem].Roughness, 0.0f, 1.0f)) { valuesChanged = true; };
	if (ImGui::SliderFloat("Metallic",				(float*)&(*m_CurrentMaterials)[currentMaterialItem].Metallic, 0.0f, 1.0f)) { valuesChanged = true; };
	if (ImGui::SliderFloat("IOR",					(float*)&(*m_CurrentMaterials)[currentMaterialItem].Ior, 1.001f, 2.0f)) { valuesChanged = true; };
	if (ImGui::SliderFloat("Clearcoat",				(float*)&(*m_CurrentMaterials)[currentMaterialItem].Clearcoat, 0.0f, 1.0f)) { valuesChanged = true; };
	if (ImGui::SliderFloat("Clearcoat Roughness",	(float*)&(*m_CurrentMaterials)[currentMaterialItem].ClearcoatRoughness, 0.0f, 1.0f)) { valuesChanged = true; };
	
	static bool editAll = false;
	ImGui::Checkbox("Edit All Values", &editAll);

	if (editAll)
	{
		for (int i = 0; i < (*m_CurrentMaterials).size(); i++)
		{
			(*m_CurrentMaterials)[i].Color				= (*m_CurrentMaterials)[currentMaterialItem].Color;
			(*m_CurrentMaterials)[i].Roughness			= (*m_CurrentMaterials)[currentMaterialItem].Roughness;
			(*m_CurrentMaterials)[i].Metallic			= (*m_CurrentMaterials)[currentMaterialItem].Metallic;
			(*m_CurrentMaterials)[i].Clearcoat			= (*m_CurrentMaterials)[currentMaterialItem].Clearcoat;
			(*m_CurrentMaterials)[i].ClearcoatRoughness = (*m_CurrentMaterials)[currentMaterialItem].ClearcoatRoughness;
			(*m_CurrentMaterials)[i].Ior				= (*m_CurrentMaterials)[currentMaterialItem].Ior;
		}

		if (valuesChanged)
		{
			// Upload to GPU
			m_SceneRenderer->GetRTSet()->GetBuffer(3)->WriteToBuffer(
				m_CurrentMaterials->data(),
				sizeof(Vulture::Material) * (*m_CurrentMaterials).size(),
				0
			);

			m_SceneRenderer->ResetFrameAccumulation();
		}
	}
	else
	{
		if (valuesChanged)
		{
			// Upload to GPU
			m_SceneRenderer->GetRTSet()->GetBuffer(3)->WriteToBuffer(
				((uint8_t*)m_CurrentMaterials->data()) + (uint8_t)sizeof(Vulture::Material) * (uint8_t)currentMaterialItem,
				sizeof(Vulture::Material),
				sizeof(Vulture::Material) * currentMaterialItem
			);

			m_SceneRenderer->ResetFrameAccumulation();
		}
	}
	ImGui::Separator();
}

void Editor::ImGuiCameraSettings()
{
	Vulture::Entity cameraEntity;
	m_CurrentScene->GetMainCamera(&cameraEntity);

	Vulture::ScriptComponent* scComp = m_CurrentScene->GetRegistry().try_get<Vulture::ScriptComponent>(cameraEntity);
	Vulture::CameraComponent* camComp = m_CurrentScene->GetRegistry().try_get<Vulture::CameraComponent>(cameraEntity);
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
			m_SceneRenderer->UpdateCamera();
		}
		ImGui::SliderFloat("Movement Speed", &camScript->m_MovementSpeed, 0.0f, 20.0f);
		ImGui::SliderFloat("Rotation Speed", &camScript->m_RotationSpeed, 0.0f, 40.0f);

		Vulture::CameraComponent* cp = &camScript->GetComponent<Vulture::CameraComponent>();
		if (ImGui::SliderFloat("FOV", &cp->FOV, 10.0f, 45.0f))
		{
			m_SceneRenderer->ResetFrameAccumulation();
			m_SceneRenderer->UpdateCamera();
		}

		float newAspectRatio = (float)m_SceneRenderer->GetViewportContentSize().width / (float)m_SceneRenderer->GetViewportContentSize().height;
		cp->SetPerspectiveMatrix(cp->FOV, newAspectRatio, 0.1f, 1000.0f);

		glm::vec3 position = cp->Translation;
		glm::vec3 rotation = cp->Rotation.GetAngles();
		bool changed = false;
		if (ImGui::InputFloat3("Position", (float*)&position)) { changed = true; };
		if (ImGui::InputFloat3("Rotation", (float*)&rotation)) { changed = true; };

		if (changed)
		{
			cp->Translation = position;
			cp->Rotation.SetAngles(rotation);

			cp->UpdateViewMatrix();
		}
	}
	ImGui::Separator();
}

void Editor::ImGuiEnvironmentMapSettings()
{
	ImGui::Separator();

	SceneRenderer::DrawInfo& drawInfo = m_SceneRenderer->GetDrawInfo();
	if (ImGui::SliderFloat("Azimuth", &drawInfo.EnvAzimuth, 0.0f, 360.0f)) { m_SceneRenderer->ResetFrameAccumulation(); };
	if (ImGui::SliderFloat("Altitude", &drawInfo.EnvAltitude, 0.0f, 360.0f)) { m_SceneRenderer->ResetFrameAccumulation(); };

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
		State::CurrentSkyboxPath = "assets/" + envMapsString[currentItem];
		State::ChangeSkybox = true;
	}

	ImGui::Separator();
}

std::vector<glm::vec3> GeneratePallet(uint32_t numberOfColors, uint32_t& seed)
{
	float hue = Vulture::Random(seed) * 2.0f * (float)M_PI;
	float hueContrast = glm::lerp(0.0f, 1.0f, Vulture::Random(seed));
	float L = glm::lerp(0.134f, 0.343f, Vulture::Random(seed));
	float LContrast = glm::lerp(0.114f, 1.505f, Vulture::Random(seed));
	float chroma = glm::lerp(0.077f, 0.179f, Vulture::Random(seed));
	float chromaContrast = glm::lerp(0.078f, 0.224f, Vulture::Random(seed));

	std::vector<glm::vec3> colors(numberOfColors);

	for (int i = 0; i < (int)numberOfColors; i++)
	{
		float linearIterator = (float)i / (numberOfColors);

		float hueOffset = hueContrast * linearIterator * 2.0f * (float)M_PI + ((float)M_PI / 4.0f);
		hueOffset *= 0.33f;

		float luminanceOffset = L + LContrast * linearIterator;
		float chromaOffset = chroma + chromaContrast * linearIterator;

		colors[i] = Vulture::OKLCHtoRGB(glm::vec3(luminanceOffset, chromaOffset, hue + hueOffset));

		colors[i] = glm::clamp(colors[i], 0.0f, 1.0f);
	}

	return colors;
}


void Editor::ImGuiPostProcessingSettings()
{
	SceneRenderer::DrawInfo& drawInfo = m_SceneRenderer->GetDrawInfo();
	ImGui::Separator();
	{
		const char* items[] = { "None", "Ink", "Posterize" };

		static int currentItem = 0;
		if (ImGui::ListBox("Current Effect", &currentItem, items, IM_ARRAYSIZE(items), IM_ARRAYSIZE(items)))
		{
			m_SceneRenderer->SetCurrentPostProcessEffect((SceneRenderer::PostProcessEffects)currentItem);
		}

		if (currentItem == (int)SceneRenderer::PostProcessEffects::Ink)
		{
			ImGui::SliderInt("Noise Center Pixel Weight", &m_SceneRenderer->GetInkPush().GetDataPtr()->NoiseCenterPixelWeight, 0, 10);
			ImGui::SliderInt("Noise Sample Range", &m_SceneRenderer->GetInkPush().GetDataPtr()->NoiseSampleRange, 0, 3);
			ImGui::SliderFloat("Luminance Bias", &m_SceneRenderer->GetInkPush().GetDataPtr()->LuminanceBias, -0.5f, 0.5f);
		}
		else if (currentItem == (int)SceneRenderer::PostProcessEffects::Posterize)
		{
			ImGui::SliderInt("Color Count", &m_SceneRenderer->GetPosterizePush().GetDataPtr()->ColorCount, 1, 8);
			glm::clamp(m_SceneRenderer->GetPosterizePush().GetDataPtr()->ColorCount, 1, 8);
			ImGui::SliderFloat("Dither Spread", &m_SceneRenderer->GetPosterizePush().GetDataPtr()->DitherSpread, 0.0f, 4.0f);
			ImGui::SliderInt("Dither Size", &m_SceneRenderer->GetPosterizePush().GetDataPtr()->DitherSize, 0, 5);

			static bool posterizeReplacePallet = false;
			if (ImGui::Checkbox("Replace Color Pallet", &posterizeReplacePallet))
			{
				State::RecompilePosterizeShader = true;
			}
			State::ReplacePalletDefineInPosterizeShader = posterizeReplacePallet;

			if (State::ReplacePalletDefineInPosterizeShader)
			{
				if (ImGui::Button("Generate pallet"))
				{
					static uint32_t x = uint32_t(m_TotalTimer.ElapsedMillis() * 100.0f);
					x += uint32_t(m_TotalTimer.ElapsedMillis() * 100.0f);
					std::vector<glm::vec3> colors = GeneratePallet(m_SceneRenderer->GetPosterizePush().GetDataPtr()->ColorCount, x);
					for (int i = 0; i < m_SceneRenderer->GetPosterizePush().GetDataPtr()->ColorCount; i++)
					{
						m_SceneRenderer->GetPosterizePush().GetDataPtr()->colors[i] = glm::vec4(colors[i], 1.0f);
					}
				}

				for (int i = 0; i < m_SceneRenderer->GetPosterizePush().GetDataPtr()->ColorCount; i++)
				{
					ImGui::ColorEdit3(("Color" + std::to_string(i + 1)).c_str(), (float*)&m_SceneRenderer->GetPosterizePush().GetDataPtr()->colors[i]);
				}
			}
		}
	}
	ImGui::Separator();
	ImGui::SeparatorText("Bloom");
	ImGui::SliderFloat("Threshold", &drawInfo.BloomInfo.Threshold, 0.0f, 3.0f);
	ImGui::SliderFloat("Strength", &drawInfo.BloomInfo.Strength, 0.0f, 3.0f);
	if (ImGui::SliderInt("Mip Count", &drawInfo.BloomInfo.MipCount, 1, 10))
	{
		m_SceneRenderer->GetBloom()->RecreateDescriptors(drawInfo.BloomInfo.MipCount, ((Vulture::Renderer::GetCurrentFrameIndex() + 1) % MAX_FRAMES_IN_FLIGHT));
	}

	ImGui::SeparatorText("Tonemapping");
	ImGui::SliderFloat("Exposure", &drawInfo.TonemapInfo.Exposure, 0.0f, 3.0f);
	ImGui::SliderFloat("Contrast", &drawInfo.TonemapInfo.Contrast, 0.0f, 3.0f);
	ImGui::SliderFloat("Brightness", &drawInfo.TonemapInfo.Brightness, 0.0f, 3.0f);
	ImGui::SliderFloat("Saturation", &drawInfo.TonemapInfo.Saturation, 0.0f, 3.0f);
	ImGui::SliderFloat("Vignette", &drawInfo.TonemapInfo.Vignette, 0.0f, 1.0f);
	ImGui::SliderFloat("Gamma", &drawInfo.TonemapInfo.Gamma, 0.0f, 2.0f);
	ImGui::SliderFloat("Temperature", &drawInfo.TonemapInfo.Temperature, -1.0f, 1.0f);
	ImGui::SliderFloat("Tint", &drawInfo.TonemapInfo.Tint, -1.0f, 1.0f);
	ImGui::ColorEdit3("Color Filter", (float*)&drawInfo.TonemapInfo.ColorFilter);
	if (ImGui::Checkbox("Chromatic Aberration", &State::UseChromaticAberration)) { m_SceneRenderer->RecompileTonemapShader(); };
	if (State::UseChromaticAberration)
	{
		ImGui::SliderInt2("Aberration Offset R", (int*)&drawInfo.TonemapInfo.AberrationOffsets[0], -5, 5);
		ImGui::SliderInt2("Aberration Offset G", (int*)&drawInfo.TonemapInfo.AberrationOffsets[1], -5, 5);
		ImGui::SliderInt2("Aberration Offset B", (int*)&drawInfo.TonemapInfo.AberrationOffsets[2], -5, 5);

		ImGui::SliderFloat("Aberration Vignette", &drawInfo.TonemapInfo.AberrationVignette, 0.0f, 10.0f);
	}
	ImGui::Text("");

	ImGui::Text("Tonemappers");
	static int currentTonemapper = 0;
	const char* tonemappers[] = { "Filmic", "Hill Aces", "Narkowicz Aces", "Exposure Mapping", "Uncharted 2", "Reinchard Extended" };
	if (ImGui::ListBox("Tonemappers", &currentTonemapper, tonemappers, IM_ARRAYSIZE(tonemappers), IM_ARRAYSIZE(tonemappers)))
	{
		State::CurrentTonemapper = (Vulture::Tonemap::Tonemappers)currentTonemapper;
		m_SceneRenderer->RecompileTonemapShader(); // TODO?
	}

	if (currentTonemapper == Vulture::Tonemap::Tonemappers::ReinchardExtended)
		ImGui::SliderFloat("White Point", &drawInfo.TonemapInfo.whitePointReinhard, 0.0f, 5.0f);

	ImGui::Separator();
}

void Editor::ImGuiPathTracingSettings()
{
	SceneRenderer::DrawInfo& drawInfo = m_SceneRenderer->GetDrawInfo();
	ImGui::Separator();

	if (ImGui::Checkbox("Use Normal Maps (Experimental)", &drawInfo.UseNormalMaps))
	{
		State::RecreateRayTracingPipeline = true;
	}
	if (ImGui::Checkbox("Use Albedo", &drawInfo.UseAlbedo))
	{
		State::RecreateRayTracingPipeline = true;
	}
	if (ImGui::Checkbox("Use Glossy Reflections", &drawInfo.UseGlossy))
	{
		State::RecreateRayTracingPipeline = true;
	}
	if (ImGui::Checkbox("Use Glass", &drawInfo.UseGlass))
	{
		State::RecreateRayTracingPipeline = true;
	}
	if (ImGui::Checkbox("Use Clearcoat", &drawInfo.UseClearcoat))
	{
		State::RecreateRayTracingPipeline = true;
	}
	if (ImGui::Checkbox("Eliminate Fireflies", &drawInfo.UseFireflies))
	{
		State::RecreateRayTracingPipeline = true;
	}
	if (ImGui::Checkbox("Show Skybox", &drawInfo.ShowSkybox))
	{
		State::RecreateRayTracingPipeline = true;
	}

	if (ImGui::Checkbox("Sample Environment Map", &drawInfo.SampleEnvMap))
	{
		State::RecreateRayTracingPipeline = true;
	}

	ImGui::Text("");

	ImGui::SliderInt("Max Depth", &drawInfo.RayDepth, 1, 20);
	ImGui::SliderInt("Samples Per Pixel", &drawInfo.TotalSamplesPerPixel, 1, 50'000);
	ImGui::SliderInt("Samples Per Frame", &drawInfo.SamplesPerFrame, 1, 40);

	if (ImGui::SliderFloat("Aliasing Jitter Strength", &drawInfo.AliasingJitterStr, 0.0f, 1.0f)) { m_SceneRenderer->ResetFrameAccumulation(); };
	if (ImGui::Checkbox("Auto Focal Length", &drawInfo.AutoDoF)) { m_SceneRenderer->ResetFrameAccumulation(); };
	if (ImGui::SliderFloat("Focal Length", &drawInfo.FocalLength, 1.0f, 100.0f)) { m_SceneRenderer->ResetFrameAccumulation(); };
	if (ImGui::SliderFloat("DoF Strength", &drawInfo.DOFStrength, 0.0f, 100.0f)) { m_SceneRenderer->ResetFrameAccumulation(); };
	ImGui::Separator();
}

void Editor::ImGuiFileSettings()
{
	Vulture::Entity cameraEntity;
	m_CurrentScene->GetMainCamera(&cameraEntity);
	
	Vulture::ScriptComponent* scComp = m_CurrentScene->GetRegistry().try_get<Vulture::ScriptComponent>(cameraEntity);
	Vulture::CameraComponent* camComp = m_CurrentScene->GetRegistry().try_get<Vulture::CameraComponent>(cameraEntity);
	CameraScript* camScript;
	
	if (scComp)
	{
		camScript = scComp->GetScript<CameraScript>(0);
	}
	
	ImGui::Separator();
	
	ImGui::Text("Resolution To Be Rendered");
	ImGui::InputInt2("", m_DrawFileInfo.Resolution);
	
	ImGui::Separator();
	if (ImGui::Button("Render"))
	{
		State::CurrentRenderState = State::RenderState::FileRender;
	
		camScript->m_CameraLocked = true; // lock the camera
		m_DrawFileInfo.Denoised = false; // reset denoised flag
	
		m_SceneRenderer->SetViewportContentSize({ (uint32_t)m_DrawFileInfo.Resolution[0], (uint32_t)m_DrawFileInfo.Resolution[1] });
		m_SceneRenderer->ResetFrameAccumulation();
		m_RecreateRendererResources = true;
	}
	
	ImGui::Separator();
}

void Editor::ImGuiDenoiserSettings()
{
	ImGui::Separator();

	if (State::Denoised)
	{
		ImGui::Checkbox("Show Denoised Image", &State::ShowDenoised);
	}

	if (ImGui::Button("Denoise"))
	{
		State::RunDenoising = true;
	}

	ImGui::Separator();
}

void Editor::ImGuiShowGBuffer()
{
	Vulture::Entity cameraEntity;
	m_CurrentScene->GetMainCamera(&cameraEntity);

	Vulture::CameraComponent* camComp = m_CurrentScene->GetRegistry().try_get<Vulture::CameraComponent>(cameraEntity);

	ImGui::Separator();
	
	ImVec2 availSpace = ImGui::GetContentRegionAvail();
	ImGui::Text("Albedo");
	float aspectRatio = camComp->GetAspectRatio();
	ImGui::Image(m_SceneRenderer->GetAlbedoDescriptor(), { 300 * aspectRatio, 300 });
	ImGui::Text("Normal");
	ImGui::Image(m_SceneRenderer->GetNormalDescriptor(), { 300 * aspectRatio, 300 });
	ImGui::Text("Roughness & Metallness");
	ImGui::Image(m_SceneRenderer->GetRoughnessDescriptor(), { 300 * aspectRatio, 300 });
	ImGui::Text("Emissive");
	ImGui::Image(m_SceneRenderer->GetEmissiveDescriptor(), { 300 * aspectRatio, 300 });
	
	ImGui::Separator();
}
