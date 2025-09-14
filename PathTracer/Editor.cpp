#define NOMINMAX
#include "Editor.h"

#include <portable-file-dialogs.h>
#include <memory>
#include <filesystem>
#include <chrono>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

void Editor::Initialize(VulkanHelper::Device device, VulkanHelper::Renderer renderer)
{
    m_Device = device;
    m_Renderer = renderer;
    m_PathTracer = PathTracer::New(device, &m_ThreadPool);

    auto selection = pfd::open_file("Select scene file", "", {
        "Scene Files", "*.gltf",
        "All Files", "*.*"
    }).result();
    VH_ASSERT(!selection.empty(), "Failed to get scene file path, Terminating.");

    m_CurrentSceneFilepath = selection[0];
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
        m_PostProcessor.SetTonemappingData({}, commandBuffer);
    });
    
    // Initialize camera with scene data
    m_InitialViewMatrix = glm::inverse(m_PathTracer.GetCameraViewInverse());
    m_InitialProjectionMatrix = glm::inverse(m_PathTracer.GetCameraProjectionInverse());
    m_Camera = FlyCamera(glm::inverse(m_PathTracer.GetCameraViewInverse()), glm::inverse(m_PathTracer.GetCameraProjectionInverse()));
    UpdateCamera();

    m_CurrentImGuiDescriptorIndex = VulkanHelper::Renderer::CreateImGuiDescriptorSet(m_PostProcessor.GetOutputImageView(), m_ImGuiSampler, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL);
}

void Editor::Draw(VulkanHelper::CommandBuffer commandBuffer)
{
    static auto renderTimer = std::chrono::high_resolution_clock::now();
    // Execute all deferred tasks before the rendering starts
    for (auto& task : m_DeferredTasks)
    {
        task.second(commandBuffer, task.first);
    }
    m_DeferredTasks.clear();

    bool allSamplesAccumulated = m_PathTracer.PathTrace(commandBuffer);
    if (!allSamplesAccumulated)
    {
        m_RenderTime += std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - renderTimer).count() * 0.001f;
    }
    renderTimer = std::chrono::high_resolution_clock::now();

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
    float scale = glm::min(viewportSize.x / pathTraceImageSize.x, viewportSize.y / pathTraceImageSize.y);

    ImGui::SameLine();
    ImGui::SetCursorPos(ImVec2((viewportSize.x - pathTraceImageSize.x * scale) / 2.0f, ImGui::GetCursorPos().y + (viewportSize.y - pathTraceImageSize.y * scale) / 2.0f));
    
    // Handle camera input when over the viewport
    if (ImGui::IsWindowHovered() && ImGui::IsWindowFocused())
    {
        ProcessCameraInput();
    }
    
    m_Renderer.RenderImGuiImage(m_CurrentImGuiDescriptorIndex, pathTraceImageSize * scale);
    ImGui::End();
    ImGui::PopStyleVar();
}

void Editor::RenderSettingsTab()
{
    ImGui::Begin("Settings");

    RenderInfo();
    RenderViewportSettings();
    RenderCameraSettings();
    RenderMaterialSettings();
    RenderPostProcessingSettings();
    RenderPathTracingSettings();
    RenderEnvMapSettings();
    RenderVolumeSettings();
    SaveToFileSettings();

    ImGui::End();
}

void Editor::RenderViewportSettings()
{
    if (!ImGui::CollapsingHeader("Viewport Settings"))
        return;
    
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
            ResizeImage(d->Width, d->Height, commandBuffer);
            m_Camera.SetAspectRatio((float)d->Width / (float)d->Height);
            m_RenderTime = 0.0f;
        });
    }
}

void Editor::RenderCameraSettings()
{
    if (!ImGui::CollapsingHeader("Camera Settings"))
        return;

    glm::vec3 position = m_Camera.GetPosition();

    float fov = m_Camera.GetFov();
    float yaw = m_Camera.GetYaw();
    float pitch = m_Camera.GetPitch();
    float movementSpeed = m_Camera.GetMovementSpeed();
    float mouseSensitivity = m_Camera.GetMouseSensitivity();
    
    bool cameraChanged = false;
    
    if (ImGui::DragFloat3("Position", &position.x, 0.1f))
    {
        m_Camera.SetPosition(position);
        cameraChanged = true;
    }
    
    if (ImGui::SliderFloat("FOV", &fov, 10.0f, 120.0f))
    {
        m_Camera.SetFov(fov);
        cameraChanged = true;
    }
    
    if (ImGui::SliderFloat("Yaw", &yaw, -180.0f, 180.0f))
    {
        m_Camera.SetRotation(yaw, pitch);
        cameraChanged = true;
    }
    
    if (ImGui::SliderFloat("Pitch", &pitch, -89.0f, 89.0f))
    {
        m_Camera.SetRotation(yaw, pitch);
        cameraChanged = true;
    }
    
    if (ImGui::SliderFloat("Movement Speed", &movementSpeed, 0.1f, 20.0f))
    {
        m_Camera.SetMovementSpeed(movementSpeed);
    }
    
    if (ImGui::SliderFloat("Mouse Sensitivity", &mouseSensitivity, 0.01f, 1.0f))
    {
        m_Camera.SetMouseSensitivity(mouseSensitivity);
    }
    
    if (cameraChanged)
    {
        UpdateCamera();
    }
        
    if (ImGui::Button("Reset Camera"))
    {
        m_Camera = FlyCamera(m_InitialViewMatrix, m_InitialProjectionMatrix);
        UpdateCamera();
        m_RenderTime = 0.0f;
    }
        
    ImGui::Text("Controls:");
    ImGui::BulletText("Mouse drag to look around");
    ImGui::BulletText("WASD to move forward/back/left/right");
    ImGui::BulletText("Space/LShift to move up/down");
}

void Editor::ResizeImage(uint32_t width, uint32_t height, VulkanHelper::CommandBuffer commandBuffer)
{
    m_PathTracer.ResizeImage(width, height, commandBuffer);
    m_PostProcessor.SetInputImage(m_PathTracer.GetOutputImageView());
    m_CurrentImGuiDescriptorIndex = VulkanHelper::Renderer::CreateImGuiDescriptorSet(m_PostProcessor.GetOutputImageView(), m_ImGuiSampler, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL);
    
    // Update camera aspect ratio when image is resized
    UpdateCamera();
}

void Editor::RenderMaterialSettings()
{
    if (!ImGui::CollapsingHeader("Material Settings"))
        return;

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
    if (ImGui::ColorEdit3("Specular Color", &selectedMaterial.SpecularColor.r, ImGuiColorEditFlags_Float))
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
    ImGui::Separator();
    if (ImGui::ColorEdit3("Medium Color", &selectedMaterial.MediumColor.r, ImGuiColorEditFlags_Float))
        materialModified = true;
    if (ImGui::ColorEdit3("Medium Emissive Color", &selectedMaterial.MediumEmissiveColor.r, ImGuiColorEditFlags_Float))
        materialModified = true;
    if (ImGui::SliderFloat("Medium Density", &selectedMaterial.MediumDensity, 0.0f, 1.0f))
        materialModified = true;
    if (ImGui::SliderFloat("Medium Anisotropy", &selectedMaterial.MediumAnisotropy, -1.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
        materialModified = true;
    
    ImGui::Separator();

    struct DataTex
    {
        uint32_t MaterialIndex;
        std::string FilePath;
    };
    std::string baseColorName = m_PathTracer.GetBaseColorTextureName((uint32_t)selectedMaterialIndex);
    if(ImGui::Button(("Base Color: " + baseColorName).c_str()))
    {
        auto selection = pfd::open_file("Select texture", "", {"Image Files","*.png *.jpg *.jpeg"}).result();
        if (!selection.empty())
        {
            PushDeferredTask(std::make_shared<DataTex>(DataTex{ (uint32_t)selectedMaterialIndex, selection[0] }), [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void> data) {
                auto* d = static_cast<DataTex*>(data.get());
                m_PathTracer.SetBaseColorTexture(d->MaterialIndex, d->FilePath, commandBuffer);
                m_RenderTime = 0.0f;
            });
        }
    }
    if (baseColorName != "Default White Texture")
    {
        ImGui::SameLine();
        ImGui::PushID("BaseColorTexture");
        if (ImGui::Button("X"))
        {
            PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
                m_PathTracer.SetBaseColorTexture((uint32_t)selectedMaterialIndex, "Default White Texture", commandBuffer);
                m_RenderTime = 0.0f;
            });
        }
        ImGui::PopID();
    }

    std::string normalMapName = m_PathTracer.GetNormalTextureName((uint32_t)selectedMaterialIndex);
    if(ImGui::Button(("Normal: " + normalMapName).c_str()))
    {
        auto selection = pfd::open_file("Select texture").result();
        if (!selection.empty())
        {
            PushDeferredTask(std::make_shared<DataTex>(DataTex{ (uint32_t)selectedMaterialIndex, selection[0] }), [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void> data) {
                auto* d = static_cast<DataTex*>(data.get());
                m_PathTracer.SetNormalTexture(d->MaterialIndex, d->FilePath, commandBuffer);
                m_RenderTime = 0.0f;
            });
        }
    }
    if (normalMapName != "Default Normal Texture")
    {
        ImGui::SameLine();
        ImGui::PushID("NormalTexture");
        if (ImGui::Button("X"))
        {
            PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
                m_PathTracer.SetNormalTexture((uint32_t)selectedMaterialIndex, "Default Normal Texture", commandBuffer);
                m_RenderTime = 0.0f;
            });
        }
        ImGui::PopID();
    }

    std::string roughnessTextureName = m_PathTracer.GetRoughnessTextureName((uint32_t)selectedMaterialIndex);
    if(ImGui::Button(("Roughness: " + roughnessTextureName).c_str()))
    {
        auto selection = pfd::open_file("Select texture").result();
        if (!selection.empty())
        {
            PushDeferredTask(std::make_shared<DataTex>(DataTex{ (uint32_t)selectedMaterialIndex, selection[0] }), [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void> data) {
                auto* d = static_cast<DataTex*>(data.get());
                m_PathTracer.SetRoughnessTexture(d->MaterialIndex, d->FilePath, commandBuffer);
                m_RenderTime = 0.0f;
            });
        }
    }
    if (roughnessTextureName != "Default White Texture")
    {
        ImGui::SameLine();
        ImGui::PushID("RoughnessTexture");
        if (ImGui::Button("X"))
        {
            PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
                m_PathTracer.SetRoughnessTexture((uint32_t)selectedMaterialIndex, "Default White Texture", commandBuffer);
                m_RenderTime = 0.0f;
            });
        }
        ImGui::PopID();
    }

    std::string metallicTextureName = m_PathTracer.GetMetallicTextureName((uint32_t)selectedMaterialIndex);
    if(ImGui::Button(("Metallic: " + metallicTextureName).c_str()))
    {
        auto selection = pfd::open_file("Select texture").result();
        if (!selection.empty())
        {
            PushDeferredTask(std::make_shared<DataTex>(DataTex{ (uint32_t)selectedMaterialIndex, selection[0] }), [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void> data) {
                auto* d = static_cast<DataTex*>(data.get());
                m_PathTracer.SetMetallicTexture(d->MaterialIndex, d->FilePath, commandBuffer);
                m_RenderTime = 0.0f;
            });
        }
    }
    if (metallicTextureName != "Default White Texture")
    {
        ImGui::SameLine();
        ImGui::PushID("MetallicTexture");
        if (ImGui::Button("X"))
        {
            PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
                m_PathTracer.SetMetallicTexture((uint32_t)selectedMaterialIndex, "Default White Texture", commandBuffer);
                m_RenderTime = 0.0f;
            });
        }
        ImGui::PopID();
    }

    std::string emissiveTextureName = m_PathTracer.GetEmissiveTextureName((uint32_t)selectedMaterialIndex);
    if(ImGui::Button(("Emissive: " + emissiveTextureName).c_str()))
    {
        auto selection = pfd::open_file("Select texture").result();
        if (!selection.empty())
        {
            PushDeferredTask(std::make_shared<DataTex>(DataTex{ (uint32_t)selectedMaterialIndex, selection[0] }), [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void> data) {
                auto* d = static_cast<DataTex*>(data.get());
                m_PathTracer.SetEmissiveTexture(d->MaterialIndex, d->FilePath, commandBuffer);
                m_RenderTime = 0.0f;
            });
        }
    }
    if (emissiveTextureName != "Default White Texture")
    {
        ImGui::SameLine();
        ImGui::PushID("EmissiveTexture");
        if (ImGui::Button("X"))
        {
            PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
                m_PathTracer.SetEmissiveTexture((uint32_t)selectedMaterialIndex, "Default White Texture", commandBuffer);
                m_RenderTime = 0.0f;
            });
        }
        ImGui::PopID();
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
            m_RenderTime = 0.0f;
        });
    }
}

void Editor::RenderPostProcessingSettings()
{
    if (!ImGui::CollapsingHeader("Post Processing Settings"))
        return;

    static float exposure = 1.0f;
    static float gamma = 2.2f;

    if(ImGui::SliderFloat("Exposure", &exposure, 0.0f, 2.0f))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PostProcessor.SetTonemappingData({exposure, gamma}, commandBuffer);
        });
    }

    if (ImGui::SliderFloat("Gamma", &gamma, 1.0f, 3.0f))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PostProcessor.SetTonemappingData({exposure, gamma}, commandBuffer);
        });
    }

    static float bloomThreshold = 2.0f;
    static float bloomStrength = 1.0f;
    static int bloomMipCount = 10;
    static float bloomFalloffRange = 5.0f;

    if (ImGui::SliderInt("Bloom Mip Count", &bloomMipCount, 1, 10, "%d", ImGuiSliderFlags_AlwaysClamp))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer, std::shared_ptr<void>) {
            m_PostProcessor.SetBloomData({ bloomThreshold, bloomStrength, (uint32_t)bloomMipCount, bloomFalloffRange });
        });
    }

    if (ImGui::SliderFloat("Bloom Threshold", &bloomThreshold, 0.0f, 10.0f))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer, std::shared_ptr<void>) {
            m_PostProcessor.SetBloomData({ bloomThreshold, bloomStrength, (uint32_t)bloomMipCount, bloomFalloffRange });
        });
    }

    if (ImGui::SliderFloat("Bloom Strength", &bloomStrength, 0.0f, 2.0f))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer, std::shared_ptr<void>) {
            m_PostProcessor.SetBloomData({ bloomThreshold, bloomStrength, (uint32_t)bloomMipCount, bloomFalloffRange });
        });
    }

    if (ImGui::SliderFloat("Bloom Falloff Range", &bloomFalloffRange, 0.0f, 10.0f))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer, std::shared_ptr<void>) {
            m_PostProcessor.SetBloomData({ bloomThreshold, bloomStrength, (uint32_t)bloomMipCount, bloomFalloffRange });
        });
    }
}

void Editor::RenderInfo()
{
    static auto frameTimer = std::chrono::high_resolution_clock::now();
    ImGui::SeparatorText("Render Info");

    ImGui::Text("Frame Time: %.3f ms", std::chrono::duration<float, std::micro>(std::chrono::high_resolution_clock::now() - frameTimer).count() * 0.001f);
    frameTimer = std::chrono::high_resolution_clock::now();

    ImGui::Text("Total Time: %.3f s", m_RenderTime);

    ImGui::Text("Samples Accumulated: %d / %d", m_PathTracer.GetSamplesAccumulated(), m_PathTracer.GetMaxSamplesAccumulated());
    // Samples accumulated can sometimes be higher than max samples which causes funny values, so clamp it to max samples
    uint32_t samplesAccumulated = glm::min(m_PathTracer.GetSamplesAccumulated(), m_PathTracer.GetMaxSamplesAccumulated());
    ImGui::Text("Estimated Time: %.3f s", m_RenderTime * (m_PathTracer.GetMaxSamplesAccumulated() - samplesAccumulated) / samplesAccumulated);

    ImGui::Text("Total Vertex Count: %u", (uint32_t)m_PathTracer.GetTotalVertexCount());
    ImGui::Text("Total Index Count: %u", (uint32_t)m_PathTracer.GetTotalIndexCount());

    if(ImGui::Button("Reset Path Tracing"))
    {
        m_PathTracer.ResetPathTracing();
        m_RenderTime = 0.0f;
    }

    if(ImGui::Button("Reload Shaders"))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.ReloadShaders(commandBuffer);
            m_RenderTime = 0.0f;
        });
    }

    if (ImGui::Button("Select Scene"))
    {
        auto selection = pfd::open_file("Select scene file", "", {
            "Scene Files", "*.gltf",
            "All Files", "*.*"
        }).result();
        if (!selection.empty())
        {
            m_CurrentSceneFilepath = selection[0];

            PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer, std::shared_ptr<void>) {
                m_PathTracer.SetScene(m_CurrentSceneFilepath);
                m_RenderTime = 0.0f;
                m_PostProcessor.SetInputImage(m_PathTracer.GetOutputImageView());
                m_CurrentImGuiDescriptorIndex = VulkanHelper::Renderer::CreateImGuiDescriptorSet(m_PostProcessor.GetOutputImageView(), m_ImGuiSampler, VulkanHelper::Image::Layout::SHADER_READ_ONLY_OPTIMAL);
                m_InitialViewMatrix = glm::inverse(m_PathTracer.GetCameraViewInverse());
                m_InitialProjectionMatrix = glm::inverse(m_PathTracer.GetCameraProjectionInverse());
                m_Camera = FlyCamera(glm::inverse(m_PathTracer.GetCameraViewInverse()), glm::inverse(m_PathTracer.GetCameraProjectionInverse()));
            });
        }
    }
}

void Editor::RenderPathTracingSettings()
{
    if (!ImGui::CollapsingHeader("Path Tracing Settings"))
        return;

    static int maxSamples = (int)m_PathTracer.GetMaxSamplesAccumulated();
    if (ImGui::DragInt("Max Samples", &maxSamples, 10, 1, INT32_MAX, "%d", ImGuiSliderFlags_AlwaysClamp))
    {
        m_PathTracer.SetMaxSamplesAccumulated((uint32_t)maxSamples);
    }

    static int samplesPerFrame = (int)m_PathTracer.GetSamplesPerFrame();
    if (ImGui::SliderInt("Samples Per Frame", &samplesPerFrame, 1, 100, "%d", ImGuiSliderFlags_AlwaysClamp))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.SetSamplesPerFrame((uint32_t)samplesPerFrame, commandBuffer);
            m_RenderTime = 0.0f;
        });
    }

    static int maxDepth = (int)m_PathTracer.GetMaxDepth();
    if (ImGui::SliderInt("Max Depth", &maxDepth, 1, 40, "%d"))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.SetMaxDepth((uint32_t)maxDepth, commandBuffer);
            m_RenderTime = 0.0f;
        });
    }

    static float maxLuminance = m_PathTracer.GetMaxLuminance();
    if (ImGui::SliderFloat("Max Luminance", &maxLuminance, 0.0f, 1000.0f, "%.1f"))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.SetMaxLuminance(maxLuminance, commandBuffer);
            m_RenderTime = 0.0f;
        });
    }

    static float focusDistance = m_PathTracer.GetFocusDistance();
    if (ImGui::SliderFloat("Focus Distance", &focusDistance, 0.0f, 10.0f, "%.2f"))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.SetFocusDistance(focusDistance, commandBuffer);
            m_RenderTime = 0.0f;
        });
    }

    static float depthOfFieldStrength = m_PathTracer.GetDepthOfFieldStrength();
    if (ImGui::SliderFloat("Depth of Field Strength", &depthOfFieldStrength, 0.0f, 2.0f, "%.2f"))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.SetDepthOfFieldStrength(depthOfFieldStrength, commandBuffer);
            m_RenderTime = 0.0f;
        });
    }

    static bool enableEnvMapMIS = m_PathTracer.IsEnvMapMISEnabled();
    if (ImGui::Checkbox("Enable Environment Map MIS", &enableEnvMapMIS))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.SetEnvMapMIS(enableEnvMapMIS, commandBuffer);
            m_RenderTime = 0.0f;
        });
    }

    static bool areRayQueriesSupported = m_Device.AreRayQueriesSupported();

    ImGui::BeginDisabled(!areRayQueriesSupported);
    static bool useRayQueries = m_PathTracer.UseRayQueries();
    if (ImGui::Checkbox("Use Ray Queries", &useRayQueries))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.SetUseRayQueries(useRayQueries, commandBuffer);
            m_RenderTime = 0.0f;
        });
    }
    ImGui::EndDisabled();

    static bool showEnvMapDirectly = m_PathTracer.IsEnvMapShownDirectly();
    if (ImGui::Checkbox("Show Environment Map Directly", &showEnvMapDirectly))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.SetEnvMapShownDirectly(showEnvMapDirectly, commandBuffer);
            m_RenderTime = 0.0f;
        });
    }

    static bool useOnlyGeometryNormals = m_PathTracer.UseOnlyGeometryNormals();
    if (ImGui::Checkbox("Use Only Geometry Normals", &useOnlyGeometryNormals))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.SetUseOnlyGeometryNormals(useOnlyGeometryNormals, commandBuffer);
            m_RenderTime = 0.0f;
        });
    }

    static bool useEnergyCompensation = m_PathTracer.UseEnergyCompensation();
    if (ImGui::Checkbox("Use Energy Compensation", &useEnergyCompensation))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.SetUseEnergyCompensation(useEnergyCompensation, commandBuffer);
            m_RenderTime = 0.0f;
        });
    }

    static bool furnaceTestMode = m_PathTracer.IsInFurnaceTestMode();
    if (ImGui::Checkbox("Furnace Test Mode", &furnaceTestMode))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.SetFurnaceTestMode(furnaceTestMode, commandBuffer);
            m_RenderTime = 0.0f;
        });
    }

    static int splitScreenCount = (int)m_PathTracer.GetSplitScreenCount();
    if (ImGui::SliderInt("Split Screen Count", &splitScreenCount, 1, 4, "%d"))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.SetSplitScreenCount((uint32_t)splitScreenCount, commandBuffer);
            m_RenderTime = 0.0f;
        });
    }
}

void Editor::RenderEnvMapSettings()
{
    if (!ImGui::CollapsingHeader("Environment Map Settings"))
        return;

    static float azimuth = m_PathTracer.GetEnvMapRotationAzimuth();
    if (ImGui::SliderFloat("Azimuth", &azimuth, 0.0f, 360.0f, "%.1f"))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.SetEnvMapAzimuth(azimuth, commandBuffer);
            m_RenderTime = 0.0f;
        });
    }

    static float altitude = m_PathTracer.GetEnvMapRotationAltitude();
    if (ImGui::SliderFloat("Altitude", &altitude, -90.0f, 90.0f, "%.1f"))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.SetEnvMapAltitude(altitude, commandBuffer);
            m_RenderTime = 0.0f;
        });
    }

    static float envMapIntensity = m_PathTracer.GetEnvironmentIntensity();
    if (ImGui::SliderFloat("Environment Map Intensity", &envMapIntensity, 0.0f, 10.0f, "%.1f"))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.SetEnvironmentIntensity(envMapIntensity, commandBuffer);
            m_RenderTime = 0.0f;
        });
    }

    static std::string envMapFilepath = m_PathTracer.GetEnvMapFilepath();

    if(ImGui::Button(("Env Map: " + envMapFilepath).c_str()))
    {
        auto selection = pfd::open_file("Select Env Map", ".", {"HDR Image Files", "*.hdr"}).result();
        if (!selection.empty())
        {
            envMapFilepath = selection[0];
            PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
                m_PathTracer.SetEnvMapFilepath(envMapFilepath, commandBuffer);
                m_RenderTime = 0.0f;
            });
        }
    }
}

void Editor::SaveToFileSettings()
{
    if (!ImGui::CollapsingHeader("Save To File"))
        return;

    static char fileName[256] = "output";
    ImGui::InputText("File Name", fileName, sizeof(fileName));

    static char savedFilename[256] = "output";

    static bool imageSaved = false;
    if (ImGui::Button("Save"))
    {
        std::string filePath;
        uint32_t counter = 1;

        // Check if folder is created
        if (!std::filesystem::exists("../../RenderedImages"))
        {
            std::filesystem::create_directories("../../RenderedImages");
        }

        filePath = "../../RenderedImages/" + std::string(fileName) + ".png";
        while(true)
        {
            if (!std::filesystem::exists(filePath))
                break;

            filePath = "../../RenderedImages/" + std::string(fileName) + "_" + std::to_string(counter++) + ".png";
        }

        PushDeferredTask(std::make_shared<std::string>(filePath), [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void> data) {
            SaveToFile(*(std::string*)data.get(), commandBuffer);
        });
        imageSaved = true;
        strcpy(savedFilename, fileName);
    }

    if (imageSaved)
        ImGui::Text("File saved to: RenderedImages/%s", savedFilename);
}

void Editor::SaveToFile(const std::string& filepath, VulkanHelper::CommandBuffer commandBuffer)
{
    VulkanHelper::Buffer::Config bufferConfig{};
    bufferConfig.Device = m_Device;
    bufferConfig.Size = (uint64_t)(m_PostProcessor.GetOutputImageView().GetImage().GetWidth() * m_PostProcessor.GetOutputImageView().GetImage().GetHeight() * 4);
    bufferConfig.Usage = VulkanHelper::Buffer::Usage::TRANSFER_DST_BIT;
    bufferConfig.CpuMapable = true;
    bufferConfig.DebugName = "Save to file Buffer";
    VulkanHelper::Buffer buffer = VulkanHelper::Buffer::New(bufferConfig).Value();

    m_PostProcessor.GetOutputImageView().GetImage().TransitionImageLayout(VulkanHelper::Image::Layout::TRANSFER_SRC_OPTIMAL, commandBuffer);
    VH_ASSERT(buffer.CopyFromImage(commandBuffer, m_PostProcessor.GetOutputImageView().GetImage()) == VulkanHelper::VHResult::OK, "Failed to copy image to buffer");

    VH_ASSERT(commandBuffer.EndRecording() == VulkanHelper::VHResult::OK, "Failed to end command buffer recording");
    VH_ASSERT(commandBuffer.SubmitAndWait() == VulkanHelper::VHResult::OK, "Failed to submit command buffer");
    VH_ASSERT(commandBuffer.BeginRecording(VulkanHelper::CommandBuffer::Usage::ONE_TIME_SUBMIT_BIT) == VulkanHelper::VHResult::OK, "Failed to begin command buffer recording");
    void* mappedData = buffer.Map().Value();
    stbi_write_png(
        filepath.c_str(),
        (int)m_PostProcessor.GetOutputImageView().GetImage().GetWidth(),
        (int)m_PostProcessor.GetOutputImageView().GetImage().GetHeight(),
        4,
        mappedData,
        (int)m_PostProcessor.GetOutputImageView().GetImage().GetWidth() * 4
    );

    buffer.Unmap();
    VulkanHelper::Buffer imageBuffer;
}

void Editor::RenderVolumeSettings()
{
    if (!ImGui::CollapsingHeader("Volume Settings"))
        return;

    const char* phaseFunctions[] = { "Henyey", "Draine", "Henyey-Plus-Draine"};

    static int selectedPhaseFunction = 0;
    if (ImGui::Combo("Phase Function", &selectedPhaseFunction, phaseFunctions, IM_ARRAYSIZE(phaseFunctions)))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.SetPhaseFunction((PathTracer::PhaseFunction)selectedPhaseFunction, commandBuffer);
            m_RenderTime = 0.0f;
        });
    }

    if (ImGui::Button("Add Volume"))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.AddVolume({}, commandBuffer);
        });
    }

    if (m_PathTracer.GetVolumes().empty())
    {
        ImGui::Text("No volumes in the scene.");
        return;
    }
    
    static int selectedVolumeIndex = 0;

    if (ImGui::Button("Remove Volume"))
    {
        PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
            m_PathTracer.RemoveVolume((uint32_t)selectedVolumeIndex, commandBuffer);
            selectedVolumeIndex = 0;
            m_RenderTime = 0.0f;
        });
    }

    const auto& volumes = m_PathTracer.GetVolumes();
    std::vector<std::string> volumeNames;
    std::vector<const char*> volumeNamesCStr;
    volumeNames.reserve(volumes.size());
    for (uint32_t i = 0; i < volumes.size(); i++)
    {
        volumeNames.push_back(std::to_string(i));
        volumeNamesCStr.push_back(volumeNames[i].c_str());
    }

    ImGui::ListBox("Volumes", &selectedVolumeIndex, volumeNamesCStr.data(), volumeNamesCStr.size(), volumeNamesCStr.size() > 10 ? 10 : (int)volumeNamesCStr.size());

    bool volumeModified = false;
    PathTracer::Volume selectedVolume = volumes[(size_t)selectedVolumeIndex];

    if (ImGui::Button("Import Density Data (.vdb)"))
    {
        static std::vector<std::string> selection;
        selection = pfd::open_file("Select Volume", ".", {"OpenVDB Files", "*.vdb"}).result();
        if (!selection.empty())
        {
            PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
                m_PathTracer.AddDensityDataToVolume((uint32_t)selectedVolumeIndex, selection[0], commandBuffer);
                m_RenderTime = 0.0f;
            });
        }
    }

    if (selectedVolume.DensityDataIndex != -1)
    {
        ImGui::SameLine();
        if (ImGui::Button("X"))
        {
            PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer commandBuffer, std::shared_ptr<void>) {
                m_PathTracer.RemoveDensityDataFromVolume((uint32_t)selectedVolumeIndex, commandBuffer);
                m_RenderTime = 0.0f;
            });
        }
    }

    ImGui::PushID("VolumeSettings");

    if (ImGui::InputFloat3("Corner Min", &selectedVolume.CornerMin.x))
        volumeModified = true;
    if (ImGui::InputFloat3("Corner Max", &selectedVolume.CornerMax.x))
        volumeModified = true;
    if (ImGui::InputFloat3("Translation", &selectedVolume.Position.x))
        volumeModified = true;
    if (ImGui::InputFloat3("Scale", &selectedVolume.Scale.x))
        volumeModified = true;

    if (ImGui::ColorEdit3("Color", &selectedVolume.Color.r, ImGuiColorEditFlags_Float))
        volumeModified = true;
    if (ImGui::ColorEdit3("Emissive Color", &selectedVolume.EmissiveColor.r, ImGuiColorEditFlags_Float))
        volumeModified = true;
    if (ImGui::SliderFloat("Density", &selectedVolume.Density, 0.0f, 1.0f))
        volumeModified = true;

    if (selectedPhaseFunction == (int)PathTracer::PhaseFunction::HENYEY_GREENSTEIN || 
        selectedPhaseFunction == (int)PathTracer::PhaseFunction::DRAINE)
    {
        if (ImGui::SliderFloat("Anisotropy", &selectedVolume.Anisotropy, -0.9999f, 0.9999f, "%.4f", ImGuiSliderFlags_AlwaysClamp))
        {
            volumeModified = true;
            selectedVolume.Anisotropy = glm::clamp(selectedVolume.Anisotropy, -0.9999f, 0.9999f);
        }
    }
    if (selectedPhaseFunction == (int)PathTracer::PhaseFunction::DRAINE)
    {
        if (ImGui::SliderFloat("Alpha", &selectedVolume.Alpha, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
        {
            volumeModified = true;
            selectedVolume.Alpha = glm::clamp(selectedVolume.Alpha, 0.0f, 1.0f);
        }
    }

    if (selectedPhaseFunction == (int)PathTracer::PhaseFunction::HENYEY_GREENSTEIN_PLUS_DRAINE)
    {
        if (ImGui::SliderFloat("Droplet Size", &selectedVolume.DropletSize, 5.0f, 50.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
        {
            volumeModified = true;
            selectedVolume.DropletSize = glm::clamp(selectedVolume.DropletSize, 5.0f, 50.0f);
        }
    }

    ImGui::PopID();

    if (volumeModified)
    {
        struct DataVol
        {
            PathTracer::Volume vol;
            int volumeIndex;
        };

        PushDeferredTask(std::make_shared<DataVol>(DataVol{ selectedVolume, (int)selectedVolumeIndex }), [this](VulkanHelper::CommandBuffer cmd, std::shared_ptr<void> data) {
            auto volumeData = std::static_pointer_cast<DataVol>(data);
            m_PathTracer.SetVolume((uint32_t)volumeData->volumeIndex, volumeData->vol, cmd);
            m_RenderTime = 0.0f;
        });
    }
}

void Editor::UpdateCamera()
{    
    // Update the path tracer with the new camera matrices (pass inverses)
    PushDeferredTask(nullptr, [this](VulkanHelper::CommandBuffer cmd, std::shared_ptr<void>) {
        glm::mat4 viewMatrix = m_Camera.GetViewMatrix();
        glm::mat4 projMatrix = m_Camera.GetProjectionMatrix();
        m_PathTracer.SetCameraViewInverse(glm::inverse(viewMatrix), cmd);
        m_PathTracer.SetCameraProjectionInverse(glm::inverse(projMatrix), cmd);
    });
}

void Editor::ProcessCameraInput()
{
    // Calculate delta time
    auto currentTime = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - m_LastFrameTime).count();
    m_LastFrameTime = currentTime;
    
    bool cameraChanged = false;
    
    // Mouse rotation (only when dragging)
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        m_IsDraggingViewport = true;
        ImVec2 mousePos = ImGui::GetMousePos();
        m_LastMousePos = {mousePos.x, mousePos.y};
    }
    
    if (m_IsDraggingViewport && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        ImVec2 mousePos = ImGui::GetMousePos();
        glm::vec2 currentMousePos = {mousePos.x, mousePos.y};
        glm::vec2 deltaPos = currentMousePos - m_LastMousePos;
        
        m_Camera.ProcessMouseMovement(deltaPos.x, deltaPos.y);

        if (m_LastMousePos != currentMousePos)
            cameraChanged = true;

        m_LastMousePos = currentMousePos;
    }

    // Keyboard movement (WASD + space/left shift for up/down)
    if (m_IsDraggingViewport && ImGui::IsKeyDown(ImGuiKey_W))
    {
        m_Camera.ProcessKeyboard(FlyCamera::Direction::FORWARD, deltaTime);
        cameraChanged = true;
    }
    if (m_IsDraggingViewport && ImGui::IsKeyDown(ImGuiKey_S))
    {
        m_Camera.ProcessKeyboard(FlyCamera::Direction::BACKWARD, deltaTime);
        cameraChanged = true;
    }
    if (m_IsDraggingViewport && ImGui::IsKeyDown(ImGuiKey_A))
    {
        m_Camera.ProcessKeyboard(FlyCamera::Direction::LEFT, deltaTime);
        cameraChanged = true;
    }
    if (m_IsDraggingViewport && ImGui::IsKeyDown(ImGuiKey_D))
    {
        m_Camera.ProcessKeyboard(FlyCamera::Direction::RIGHT, deltaTime);
        cameraChanged = true;
    }
    if (m_IsDraggingViewport && ImGui::IsKeyDown(ImGuiKey_Space))
    {
        m_Camera.ProcessKeyboard(FlyCamera::Direction::UP, deltaTime);
        cameraChanged = true;
    }
    if (m_IsDraggingViewport && ImGui::IsKeyDown(ImGuiKey_LeftShift))
    {
        m_Camera.ProcessKeyboard(FlyCamera::Direction::DOWN, deltaTime);
        cameraChanged = true;
    }
    
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        m_IsDraggingViewport = false;
    }
    
    if (cameraChanged)
    {
        UpdateCamera();
        m_RenderTime = 0.0f;
    }
}