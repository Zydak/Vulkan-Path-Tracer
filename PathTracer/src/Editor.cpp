#include "pch.h"
#include "Editor.h"

#include "State.h"
#include "CameraScript.h"
#include "Components.h"

#include "Vulture.h"


void Editor::Init()
{
	m_PathTracer.Init();
	m_PostProcessor.Init(m_PathTracer.GetOutputImage());

	Vulture::Renderer::SetImGuiFunction([this]() { RenderImGui(); });

	m_PathTracerOutputImageSet = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetLinearSamplerHandle(), m_PathTracer.GetOutputImage()->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Editor::Destroy()
{

}

void Editor::SetCurrentScene(Vulture::Scene* scene)
{
	m_CurrentScene = scene;

	m_PathTracer.SetScene(scene);
}

void Editor::Render()
{
	if (m_ImGuiViewportResized)
	{
		m_PathTracer.Resize(m_ViewportSize);
		m_PostProcessor.Resize(m_ViewportSize, m_PathTracer.GetOutputImage());
		UpdateNodeImages();
		m_ImGuiViewportResized = false;
		m_PathTracerOutputImageSet = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetLinearSamplerHandle(), m_PostProcessor.GetOutputImage()->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	else
	{
		if (Vulture::Renderer::BeginFrame())
		{
			bool rayTracingFinished = !m_PathTracer.Render();
			m_PathTracer.GetOutputImage()->TransitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, Vulture::Renderer::GetCurrentCommandBuffer());

			m_PostProcessor.Evaluate();
			m_PostProcessor.Render();

			Vulture::Renderer::ImGuiPass();

			Vulture::Renderer::EndFrame();
			m_PostProcessor.EndFrame();
		}
		else
		{
			m_PathTracer.Resize(m_ViewportSize);
			m_PostProcessor.Resize(m_ViewportSize, m_PathTracer.GetOutputImage());
			UpdateNodeImages();
			m_PathTracerOutputImageSet = ImGui_ImplVulkan_AddTexture(Vulture::Renderer::GetLinearSamplerHandle(), m_PostProcessor.GetOutputImage()->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
	}
}

Editor::Editor()
{

}

Editor::~Editor()
{

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

	ImGuiRenderViewport();

	ImGui::Begin("Settings");

	ImGui::End();
}

void Editor::ImGuiRenderViewport()
{
	Vulture::Entity cameraEntity = PerspectiveCameraComponent::GetMainCameraEntity(m_CurrentScene);

	Vulture::ScriptComponent* scComp = m_CurrentScene->GetRegistry().try_get<Vulture::ScriptComponent>(cameraEntity);
	CameraScript* camScript;

	if (scComp)
	{
		camScript = scComp->GetScript<CameraScript>(0);
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("Preview Viewport");

	if (State::CurrentRenderState == State::RenderState::PreviewRender)
	{
		if (ImGui::IsWindowHovered() && (ImGui::IsWindowDocked()))
			camScript->m_CameraLocked = false;
		else
			camScript->m_CameraLocked = true;
	}

	static ImVec2 prevViewportSize = { 0, 0 };
	ImVec2 viewportContentSize = ImGui::GetContentRegionAvail();
	m_ViewportSize = { (uint32_t)viewportContentSize.x, (uint32_t)viewportContentSize.y };
	if (viewportContentSize.x != prevViewportSize.x || viewportContentSize.y != prevViewportSize.y)
	{
		m_ImGuiViewportResized = true;
		prevViewportSize = viewportContentSize;
	}

	ImGui::Image(m_PathTracerOutputImageSet, viewportContentSize);

	ImGui::End();
	ImGui::PopStyleVar();

	m_PostProcessor.RenderGraph();
}

void Editor::UpdateNodeImages()
{
	ImFlow::ImNodeFlow* handler = m_PostProcessor.GetGridHandler();
	for (auto& node : handler->getNodes())
	{
		// Update Input Nodes
		{
			PathTracerOutputNode* inputNode = dynamic_cast<PathTracerOutputNode*>(node.second.get());
			if (inputNode != nullptr)
			{
				inputNode->UpdateImage(m_PathTracer.GetOutputImage());
			}
		}

		// Update Output Nodes
		{
			OutputNode* outputNode = dynamic_cast<OutputNode*>(node.second.get());
			if (outputNode != nullptr)
			{
				outputNode->UpdateImage(m_PostProcessor.GetOutputImage());
			}
		}

		// Update Bloom Nodes
		{
			BloomNode* bloomNode = dynamic_cast<BloomNode*>(node.second.get());
			if (bloomNode != nullptr)
			{
				bloomNode->CreateImage(m_ViewportSize);
			}
		}

		// Update Tonemap Nodes
		{
			TonemapNode* tonemapNode = dynamic_cast<TonemapNode*>(node.second.get());
			if (tonemapNode != nullptr)
			{
				tonemapNode->CreateImage(m_ViewportSize);
			}
		}
	}
}
