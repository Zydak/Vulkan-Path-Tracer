#include "Application.h"

Application::Application()
{
    // Initialize Vulkan instance
    m_Instance = VulkanHelper::Instance::New({true}).Value();
    
    // Create a window
    m_Window = VulkanHelper::Window::New({
        .Instance = m_Instance,
        .Width = 800,
        .Height = 600,
        .Name = "Vulkan Path Tracer",
        .Resizable = true
    }).Value();

    // Get Physical Devices
    auto physicalDevices = m_Instance.GetSuitablePhysicalDevices();
    for (const auto& device : physicalDevices)
    {
        if (device.IsDiscrete())
        {
            // Create a logical device for the first discrete GPU found
            m_Device = VulkanHelper::Device::New({device, {m_Window}, m_Instance, true}).Value();
            break;
        }
    }

    // Create Renderer
    m_Renderer = VulkanHelper::Renderer::New({m_Device, m_Window}).Value();

    // Create Path Tracer
    m_PathTracer = PathTracer::New(m_Device);
    m_PathTracer.SetScene("../../Assets/VikingRoom.gltf");

    // Create ImGui sampler
    VulkanHelper::Sampler::Config samplerConfig;
    samplerConfig.Device = m_Device;
    samplerConfig.AddressMode = VulkanHelper::Sampler::AddressMode::CLAMP_TO_EDGE;
    samplerConfig.MinFilter = VulkanHelper::Sampler::Filter::LINEAR;
    samplerConfig.MagFilter = VulkanHelper::Sampler::Filter::LINEAR;
    samplerConfig.MipmapMode = VulkanHelper::Sampler::MipmapMode::LINEAR;
    m_ImGuiSampler = VulkanHelper::Sampler::New(samplerConfig).Value();

    m_Renderer.CreateImGuiDescriptorSet(m_PathTracer.GetOutputImageView(), m_ImGuiSampler, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL);
}

void Application::Run()
{
    while(!m_Window.WantsToClose())
    {
        VulkanHelper::Window::PollEvents();

        VulkanHelper::CommandBuffer commandBuffer = m_Renderer.BeginFrame(nullptr).Value();

        m_PathTracer.PathTrace(commandBuffer);

        // Transition output image to shader read-only optimal layout for imgui rendering
        m_PathTracer.GetOutputImage().TransitionImageLayout(VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL, commandBuffer);

        m_Renderer.BeginImGuiRendering();
        ImGuiID dockspaceID = ImGui::GetID("Dockspace");
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::DockSpaceOverViewport(dockspaceID, viewport);

	    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Viewport");
        glm::vec2 pathTraceImageSize = {m_PathTracer.GetOutputImage().GetWidth(), m_PathTracer.GetOutputImage().GetHeight()};
        glm::vec2 viewportSize = {ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y};

        float scale = std::min(viewportSize.x / pathTraceImageSize.x, viewportSize.y / pathTraceImageSize.y);

        ImGui::SameLine();
        ImGui::SetCursorPos(ImVec2((viewportSize.x - pathTraceImageSize.x * scale) / 2.0f, ImGui::GetCursorPos().y + (viewportSize.y - pathTraceImageSize.y * scale) / 2.0f));
        m_Renderer.RenderImGuiImage(0, pathTraceImageSize * scale);
        ImGui::End();
	    ImGui::PopStyleVar();
        m_Renderer.EndImGuiRendering();

        VH_ASSERT(m_Renderer.EndFrame(nullptr) == VulkanHelper::VHResult::OK, "Failed to end frame rendering");
    }

    m_Device.WaitUntilIdle();
}
