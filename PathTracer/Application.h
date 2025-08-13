#pragma once

#include "VulkanHelper.h"

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
};