#include "Application.h"

Application::Application()
{
    // Initialize Vulkan instance
    m_Instance = VulkanHelper::Instance::New({true}).Value();
    
    // Create a window
    m_Window = VulkanHelper::Window::New({
        .Instance = m_Instance,
        .Width = 1600,
        .Height = 800,
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
    
    VulkanHelper::Shader::InitializeSession("../../../PathTracer/Shaders/");

    m_LookupTableCalculator = LookupTableCalculator::New(m_Device);
    m_LookupTableCalculator.CalculateReflectionEnergyLossGPU({64, 64, 64}, 1000);

    // Create Renderer
    m_Renderer = VulkanHelper::Renderer::New({m_Device, m_Window}).Value();

    // Create Editor
    m_Editor.Initialize(m_Device, m_Renderer);
}

void Application::Run()
{
    while(!m_Window.WantsToClose())
    {
        VulkanHelper::Window::PollEvents();

        auto beginFrameRes = m_Renderer.BeginFrame(nullptr);
        if (beginFrameRes.HasValue())
        {
            VulkanHelper::CommandBuffer commandBuffer = beginFrameRes.Value();

            m_Editor.Draw(commandBuffer);

            VH_ASSERT(m_Renderer.EndFrame(nullptr) == VulkanHelper::VHResult::OK, "Failed to end frame rendering");
        }
    }

    m_Device.WaitUntilIdle();
}
