#pragma once

#include "VulkanHelper.h"

#include "PathTracer.h"
#include "PostProcessor.h"
#include "FlyCamera.h"

#include <memory>
#include <chrono>

class Editor
{
public:
    void Initialize(VulkanHelper::Device device, VulkanHelper::Renderer renderer);

    Editor() = default;

    void Draw(VulkanHelper::CommandBuffer commandBuffer);

private:

    void RenderViewportTab();

    void RenderSettingsTab();
    void RenderInfo();
    void RenderViewportSettings();
    void RenderCameraSettings();
    void RenderMaterialSettings();
    void RenderPostProcessingSettings();
    void RenderPathTracingSettings();
    void RenderEnvMapSettings();
    void RenderVolumeSettings();
    void SaveToFileSettings();

    void SaveToFile(const std::string& filepath, VulkanHelper::CommandBuffer commandBuffer);
    void ResizeImage(uint32_t width, uint32_t height, VulkanHelper::CommandBuffer commandBuffer);
    void UpdateCamera();
    void ProcessCameraInput();

    VulkanHelper::Device m_Device;
    VulkanHelper::Renderer m_Renderer;
    VulkanHelper::ThreadPool m_ThreadPool{4};
    PathTracer m_PathTracer;
    PostProcessor m_PostProcessor;
    VulkanHelper::Sampler m_ImGuiSampler;
    uint32_t m_CurrentImGuiDescriptorIndex = 0;
    float m_RenderTime = 0.0f;
    std::string m_CurrentSceneFilepath;

    // Camera system
    FlyCamera m_Camera;
    bool m_IsDraggingViewport = false;
    glm::vec2 m_LastMousePos = {0.0f, 0.0f};
    std::chrono::steady_clock::time_point m_LastFrameTime = std::chrono::steady_clock::now();

    // A lot of vulkan commands can't be called when the render pass is active. And because ImGui
    // Is an immediate mode GUI, they have to be deferred to the beginning of the next frame.
    std::vector<std::pair<std::shared_ptr<void>, std::function<void(VulkanHelper::CommandBuffer, std::shared_ptr<void> data)>>> m_DeferredTasks;
    void PushDeferredTask(std::shared_ptr<void> data, std::function<void(VulkanHelper::CommandBuffer, std::shared_ptr<void> data)> task)
    {
        m_DeferredTasks.push_back({ data, task });
    }
};