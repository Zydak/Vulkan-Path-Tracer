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
}

void Application::Run()
{
    while(!m_Window.WantsToClose())
    {
        VulkanHelper::Window::PollEvents();

        VulkanHelper::CommandBuffer* commandBuffer = m_Renderer.BeginFrame(nullptr).Value();

        (void)commandBuffer; // Use commandBuffer for rendering commands here

        VH_ASSERT(m_Renderer.EndFrame(nullptr) == VulkanHelper::VHResult::OK, "Failed to end frame rendering");
    }

    m_Device.WaitUntilIdle();
}
