#include "Application.h"

#include <stb_image_write.h>
#include <filesystem>
#include <fstream>

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

    if (!std::filesystem::exists("../../../Assets/LookupTables/ReflectionLookup.bin"))
    {
        // Create directory
        std::filesystem::create_directories("../../../Assets/LookupTables/");

        m_LookupTableCalculator = LookupTableCalculator::New(m_Device);
        std::vector<float> data = m_LookupTableCalculator.CalculateReflectionEnergyLossGPU({64, 64, 32}, 10'000'000);

        // Write lookup to file
        std::ofstream file("../../../Assets/LookupTables/ReflectionLookup.bin", std::ios::binary);
        file.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(float));
    }

    if (!std::filesystem::exists("../../../Assets/LookupTables/RefractionLookupHitFromOutside.bin"))
    {
        // Create directory
        std::filesystem::create_directories("../../../Assets/LookupTables/");

        m_LookupTableCalculator = LookupTableCalculator::New(m_Device);
        std::vector<float> data = m_LookupTableCalculator.CalculateRefractionEnergyLossGPU({128, 128, 32}, 1'000'000, true);

        // Write lookup to file
        std::ofstream file("../../../Assets/LookupTables/RefractionLookupHitFromOutside.bin", std::ios::binary);
        file.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(float));
    }

    if (!std::filesystem::exists("../../../Assets/LookupTables/RefractionLookupHitFromInside.bin"))
    {
        // Create directory
        std::filesystem::create_directories("../../../Assets/LookupTables/");

        m_LookupTableCalculator = LookupTableCalculator::New(m_Device);
        std::vector<float> data = m_LookupTableCalculator.CalculateRefractionEnergyLossGPU({128, 128, 32}, 1'000'000, false);

        // Write lookup to file
        std::ofstream file("../../../Assets/LookupTables/RefractionLookupHitFromInside.bin", std::ios::binary);
        file.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(float));
    }

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
