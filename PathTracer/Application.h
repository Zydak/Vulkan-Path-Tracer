#pragma once

#include "VulkanHelper.h"

#include "PathTracer.h"
#include "PostProcessor.h"

class Application
{
public:

    Application();

    void Run();

private:
    VulkanHelper::Instance m_Instance;
    VulkanHelper::Window m_Window;
    VulkanHelper::Device m_Device;
    VulkanHelper::Renderer m_Renderer;

    PathTracer m_PathTracer;
    PostProcessor m_PostProcessor;
    bool m_TonemappingDataChanged = true;
    VulkanHelper::Sampler m_ImGuiSampler;
};