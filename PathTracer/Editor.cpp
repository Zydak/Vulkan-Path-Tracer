#include "Editor.h"

#include <portable-file-dialogs.h>
#include <memory>

void Editor::Initialize(VulkanHelper::Device device, VulkanHelper::Renderer renderer)
{
    m_Device = device;
    m_Renderer = renderer;
    m_PathTracer = PathTracer::New(device, &m_ThreadPool);

    auto selection = pfd::open_file("Select scene file", "", {
        "Scene Files", "*.gltf"
    }).result();
    VH_ASSERT(!selection.empty(), "Failed to get scene file path, Terminating.");

    m_PathTracer.SetScene(selection[0]);
    // Create ImGui sampler
    VulkanHelper::Sampler::Config samplerConfig;
    samplerConfig.Device = device;
    samplerConfig.AddressMode = VulkanHelper::Sampler::AddressMode::CLAMP_TO_EDGE;
    samplerConfig.MinFilter = VulkanHelper::Sampler::Filter::LINEAR;
    samplerConfig.MagFilter = VulkanHelper::Sampler::Filter::LINEAR;
    samplerConfig.MipmapMode = VulkanHelper::Sampler::MipmapMode::LINEAR;
    m_ImGuiSampler = VulkanHelper::Sampler::New(samplerConfig).Value();

    m_PostProcessor = PostProcessor::New(device);
    m_PostProcessor.SetInputImage(m_PathTracer.GetOutputImageView());

    PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
        m_PostProcessor.SetTonemappingData({0.5f}, commandBuffer);
    });

    m_CurrentImGuiDescriptorIndex = VulkanHelper::Renderer::CreateImGuiDescriptorSet(m_PostProcessor.GetOutputImageView(), m_ImGuiSampler, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL);
}

void Editor::Draw(VulkanHelper::CommandBuffer commandBuffer)
{
    // Execute all deferred tasks before the rendering starts
    for (auto& task : m_DeferredTasks)
    {
        task.second(commandBuffer, task.first);
    }
    m_DeferredTasks.clear();

    m_PathTracer.PathTrace(commandBuffer);
    m_PostProcessor.PostProcess(commandBuffer);

    // Transition output image to shader read-only optimal layout for imgui rendering
    m_PostProcessor.GetOutputImageView().GetImage().TransitionImageLayout(VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL, commandBuffer);

    m_Renderer.BeginImGuiRendering();
    ImGuiID dockspaceID = ImGui::GetID("Dockspace");
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockSpaceOverViewport(dockspaceID, viewport);

    RenderViewportTab();
    RenderSettingsTab();
    
    m_Renderer.EndImGuiRendering();
}

void Editor::RenderViewportTab()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Viewport");
    glm::vec2 pathTraceImageSize = {m_PostProcessor.GetOutputImageView().GetWidth(), m_PostProcessor.GetOutputImageView().GetHeight()};
    glm::vec2 viewportSize = {ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y};

    // Don't change dimensions of the image, scale it so that it always fits the screen
    float scale = std::min(viewportSize.x / pathTraceImageSize.x, viewportSize.y / pathTraceImageSize.y);

    ImGui::SameLine();
    ImGui::SetCursorPos(ImVec2((viewportSize.x - pathTraceImageSize.x * scale) / 2.0f, ImGui::GetCursorPos().y + (viewportSize.y - pathTraceImageSize.y * scale) / 2.0f));
    m_Renderer.RenderImGuiImage(m_CurrentImGuiDescriptorIndex, pathTraceImageSize * scale);
    ImGui::End();
    ImGui::PopStyleVar();
}

void Editor::RenderSettingsTab()
{
    ImGui::Begin("Settings");

    RenderInfo();
    RenderViewportSettings();
    RenderMaterialSettings();
    RenderPostProcessingSettings();
    RenderPathTracingSettings();

    ImGui::End();
}

void Editor::RenderViewportSettings()
{
    ImGui::SeparatorText("Viewport Settings");
    static int width = (int)m_PathTracer.GetOutputImageView().GetWidth();
    static int height = (int)m_PathTracer.GetOutputImageView().GetHeight();

    ImGui::InputInt("Width", &width);
    ImGui::InputInt("Height", &height);

    if (ImGui::Button("Apply"))
    {
        struct Data
        {
            uint32_t Width;
            uint32_t Height;
        };

        PushDeferredTask(std::make_shared<Data>(Data{ (uint32_t)width, (uint32_t)height }), [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void> data) {
            Data* d = (Data*)data.get();
            m_Device.WaitUntilIdle();
            ResizeImage(d->Width, d->Height, commandBuffer);
        });
    }
}

void Editor::ResizeImage(uint32_t width, uint32_t height, VulkanHelper::CommandBuffer commandBuffer)
{
    m_PathTracer.ResizeImage(width, height, commandBuffer);
    m_PostProcessor.SetInputImage(m_PathTracer.GetOutputImageView());
    m_CurrentImGuiDescriptorIndex = VulkanHelper::Renderer::CreateImGuiDescriptorSet(m_PostProcessor.GetOutputImageView(), m_ImGuiSampler, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL);
}

void Editor::RenderMaterialSettings()
{
    ImGui::SeparatorText("Material Settings");
    const auto& materials = m_PathTracer.GetMaterials();
    std::vector<const char*> materialNames;
    for (uint32_t i = 0; i < materials.size(); i++)
        materialNames.push_back(m_PathTracer.GetMaterialName(i).c_str());

    static int selectedMaterialIndex = 0;
    ImGui::ListBox("Materials", &selectedMaterialIndex, materialNames.data(), materialNames.size(), materialNames.size() > 10 ? 10 : (int)materialNames.size());

    bool materialModified = false;
    PathTracer::Material selectedMaterial = m_PathTracer.GetMaterial((uint32_t)selectedMaterialIndex);

    if (ImGui::ColorEdit3("Base Color", &selectedMaterial.BaseColor.r, ImGuiColorEditFlags_Float))
        materialModified = true;
    if (ImGui::ColorEdit3("Emissive Color", &selectedMaterial.EmissiveColor.r, ImGuiColorEditFlags_Float))
        materialModified = true;
    if (ImGui::SliderFloat("Metallic", &selectedMaterial.Metallic, 0.0f, 1.0f))
        materialModified = true;
    if (ImGui::SliderFloat("Roughness", &selectedMaterial.Roughness, 0.0f, 1.0f))
        materialModified = true;
    if (ImGui::SliderFloat("IOR", &selectedMaterial.IOR, 1.0f, 2.0f))
        materialModified = true;
    if (ImGui::SliderFloat("Transmission", &selectedMaterial.Transmission, 0.0f, 1.0f))
        materialModified = true;
    if (ImGui::SliderFloat("Anisotropy", &selectedMaterial.Anisotropy, 0.0f, 1.0f))
        materialModified = true;
    if (ImGui::SliderFloat("Anisotropy Rotation", &selectedMaterial.AnisotropyRotation, 0.0f, 360.0f))
        materialModified = true;


    struct DataTex
    {
        uint32_t MaterialIndex;
        std::string FilePath;
    };
    if(ImGui::Button(("Base Color: " + m_PathTracer.GetBaseColorTextureName((uint32_t)selectedMaterialIndex)).c_str()))
    {
        auto selection = pfd::open_file("Select texture", "", {"Image Files","*.png *.jpg *.jpeg"}).result();
        if (!selection.empty())
        {
            PushDeferredTask(std::make_shared<DataTex>(DataTex{ (uint32_t)selectedMaterialIndex, selection[0] }), [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void> data) {
                m_Device.WaitUntilIdle();
                auto* d = static_cast<DataTex*>(data.get());
                m_PathTracer.SetBaseColorTexture(d->MaterialIndex, d->FilePath, commandBuffer);
            });
        }
    }
    if(ImGui::Button(("Normal: " + m_PathTracer.GetNormalTextureName((uint32_t)selectedMaterialIndex)).c_str()))
    {
        auto selection = pfd::open_file("Select texture").result();
        if (!selection.empty())
        {
            PushDeferredTask(std::make_shared<DataTex>(DataTex{ (uint32_t)selectedMaterialIndex, selection[0] }), [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void> data) {
                m_Device.WaitUntilIdle();
                auto* d = static_cast<DataTex*>(data.get());
                m_PathTracer.SetNormalTexture(d->MaterialIndex, d->FilePath, commandBuffer);
            });
        }
    }
    if(ImGui::Button(("Roughness: " + m_PathTracer.GetRoughnessTextureName((uint32_t)selectedMaterialIndex)).c_str()))
    {
        auto selection = pfd::open_file("Select texture").result();
        if (!selection.empty())
        {
            PushDeferredTask(std::make_shared<DataTex>(DataTex{ (uint32_t)selectedMaterialIndex, selection[0] }), [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void> data) {
                m_Device.WaitUntilIdle();
                auto* d = static_cast<DataTex*>(data.get());
                m_PathTracer.SetRoughnessTexture(d->MaterialIndex, d->FilePath, commandBuffer);
            });
        }
    }
    if(ImGui::Button(("Metallic: " + m_PathTracer.GetMetallicTextureName((uint32_t)selectedMaterialIndex)).c_str()))
    {
        auto selection = pfd::open_file("Select texture").result();
        if (!selection.empty())
        {
            PushDeferredTask(std::make_shared<DataTex>(DataTex{ (uint32_t)selectedMaterialIndex, selection[0] }), [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void> data) {
                m_Device.WaitUntilIdle();
                auto* d = static_cast<DataTex*>(data.get());
                m_PathTracer.SetMetallicTexture(d->MaterialIndex, d->FilePath, commandBuffer);
            });
        }
    }
    if(ImGui::Button(("Emissive: " + m_PathTracer.GetEmissiveTextureName((uint32_t)selectedMaterialIndex)).c_str()))
    {
        auto selection = pfd::open_file("Select texture").result();
        if (!selection.empty())
        {
            PushDeferredTask(std::make_shared<DataTex>(DataTex{ (uint32_t)selectedMaterialIndex, selection[0] }), [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void> data) {
                m_Device.WaitUntilIdle();
                auto* d = static_cast<DataTex*>(data.get());
                m_PathTracer.SetEmissiveTexture(d->MaterialIndex, d->FilePath, commandBuffer);
            });
        }
    }

    if (materialModified)
    {
        struct DataMat
        {
            PathTracer::Material mat;
            int materialIndex;
        };
        PushDeferredTask(std::make_shared<DataMat>(DataMat{ selectedMaterial, selectedMaterialIndex }), [this](VulkanHelper::CommandBuffer cmd, std::shared_ptr<void> data) {
            DataMat* d = static_cast<DataMat*>(data.get());
            m_PathTracer.SetMaterial((uint32_t)d->materialIndex, d->mat, cmd);
        });
    }
}

void Editor::RenderPostProcessingSettings()
{
    ImGui::SeparatorText("Post Processing Settings");

    static float exposure = 0.5f;
    if(ImGui::SliderFloat("Exposure", &exposure, 0.0f, 2.0f))
    {
        PushDeferredTask(std::make_shared<float>(exposure), [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void> data) {
            float newExposure = *static_cast<float*>(data.get());
            m_PostProcessor.SetTonemappingData({newExposure}, commandBuffer);
        });
    }
}

void Editor::RenderInfo()
{
    static auto timer = std::chrono::high_resolution_clock::now();
    ImGui::SeparatorText("Render Info");

    ImGui::Text("Time: %.3f ms", std::chrono::duration<float, std::micro>(std::chrono::high_resolution_clock::now() - timer).count() * 0.001f);
    timer = std::chrono::high_resolution_clock::now();

    ImGui::Text("Samples Accumulated: %d", m_PathTracer.GetSamplesAccumulated());

    if(ImGui::Button("Reset Path Tracing"))
    {
        m_PathTracer.ResetPathTracing();
    }
}

void Editor::RenderPathTracingSettings()
{
    ImGui::SeparatorText("Path Tracing Settings");

    static int maxSamples = (int)m_PathTracer.GetMaxSamplesAccumulated();
    if (ImGui::DragInt("Max Samples", &maxSamples, 10, 0, INT32_MAX, "%d", ImGuiSliderFlags_AlwaysClamp))
    {
        m_PathTracer.SetMaxSamplesAccumulated((uint32_t)maxSamples);
    }

    static int samplesPerFrame = (int)m_PathTracer.GetSamplesPerFrame();
    if (ImGui::SliderInt("Samples Per Frame", &samplesPerFrame, 1, 100, "%d", ImGuiSliderFlags_AlwaysClamp))
    {
        PushDeferredTask(std::make_shared<uint32_t>((uint32_t)samplesPerFrame), [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void> data) {
            uint32_t newSamplesPerFrame = *static_cast<uint32_t*>(data.get());
            m_PathTracer.SetSamplesPerFrame(newSamplesPerFrame, commandBuffer);
        });
    }

    static int maxDepth = (int)m_PathTracer.GetMaxDepth();
    if (ImGui::SliderInt("Max Depth", &maxDepth, 1, 40, "%d", ImGuiSliderFlags_AlwaysClamp))
    {
        PushDeferredTask(std::make_shared<uint32_t>((uint32_t)maxDepth), [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void> data) {
            uint32_t newMaxDepth = *static_cast<uint32_t*>(data.get());
            m_PathTracer.SetMaxDepth(newMaxDepth, commandBuffer);
        });
    }

    static float maxLuminance = m_PathTracer.GetMaxLuminance();
    if (ImGui::SliderFloat("Max Luminance", &maxLuminance, 0.0f, 1000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
    {
        PushDeferredTask(std::make_shared<float>(maxLuminance), [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void> data) {
            float newMaxLuminance = *static_cast<float*>(data.get());
            m_PathTracer.SetMaxLuminance(newMaxLuminance, commandBuffer);
        });
    }
}
