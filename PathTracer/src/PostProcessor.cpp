// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "PostProcessor.h"

void PostProcessor::Init(Vulture::Image* inputImage)
{
	m_ViewportSize = inputImage->GetImageSize();
	m_InputImage = inputImage;

	CreateImages();
}

void PostProcessor::Destroy()
{
	m_CurrentScene = nullptr;

	m_ViewportSize = { 900, 900 };
	m_InputImage = nullptr;
	m_OutputImage.Destroy();
	m_BloomImage.Destroy();

	m_Bloom.Destroy();
	m_Tonemapper.Destroy();
}

void PostProcessor::SetScene(Vulture::Scene* currentScene)
{
	m_CurrentScene = currentScene;

	auto viewBloom = currentScene->GetRegistry().view<Vulture::BloomSettingsComponent>();
	Vulture::BloomSettingsComponent* bloomSettings = nullptr;
	for (auto& entity : viewBloom)
	{
		VL_CORE_ASSERT(bloomSettings == nullptr, "Can't have more than one tonemap settings inside a scene!");
		bloomSettings = &currentScene->GetRegistry().get<Vulture::BloomSettingsComponent>(entity);
	}

	// No settings found, create one
	if (bloomSettings == nullptr)
	{
		auto entity = currentScene->CreateEntity();
		bloomSettings = &entity.AddComponent<Vulture::BloomSettingsComponent>();
	}

	Vulture::Bloom::CreateInfo bloomInfo{};
	bloomInfo.InputImage = m_InputImage;
	bloomInfo.OutputImage = &m_BloomImage;
	m_Bloom.Init(bloomInfo);

	auto viewTonemap = currentScene->GetRegistry().view<Vulture::TonemapperSettingsComponent>();
	Vulture::TonemapperSettingsComponent* tonemapSettings = nullptr;
	for (auto& entity : viewTonemap)
	{
		VL_CORE_ASSERT(tonemapSettings == nullptr, "Can't have more than one tonemap settings inside a scene!");
		tonemapSettings = &currentScene->GetRegistry().get<Vulture::TonemapperSettingsComponent>(entity);
	}

	// No settings found, create one
	if (tonemapSettings == nullptr)
	{
		auto entity = currentScene->CreateEntity();
		tonemapSettings = &entity.AddComponent<Vulture::TonemapperSettingsComponent>();
	}

	Vulture::Tonemap::CreateInfo tonemapInfo{};
	tonemapInfo.InputImage = &m_BloomImage;
	tonemapInfo.OutputImage = &m_OutputImage;

	m_Tonemapper.Init(tonemapInfo);
}

void PostProcessor::Render()
{
	if (m_OutputImage.GetLayout() != VK_IMAGE_LAYOUT_GENERAL)
		m_OutputImage.TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL, Vulture::Renderer::GetCurrentCommandBuffer());

	auto viewBloom = m_CurrentScene->GetRegistry().view<Vulture::BloomSettingsComponent>();
	Vulture::BloomSettingsComponent* bloomSettings = nullptr;
	for (auto& entity : viewBloom)
	{
		VL_CORE_ASSERT(bloomSettings == nullptr, "Can't have more than one tonemap settings inside a scene!");
		bloomSettings = &m_CurrentScene->GetRegistry().get<Vulture::BloomSettingsComponent>(entity);
	}

	// No settings found, create one
	if (bloomSettings == nullptr)
	{
		auto entity = m_CurrentScene->CreateEntity();
		bloomSettings = &entity.AddComponent<Vulture::BloomSettingsComponent>();
	}

	m_Bloom.Run(bloomSettings->Settings, Vulture::Renderer::GetCurrentCommandBuffer());

	auto viewTonemap = m_CurrentScene->GetRegistry().view<Vulture::TonemapperSettingsComponent>();
	Vulture::TonemapperSettingsComponent* tonemapSettings = nullptr;
	for (auto& entity : viewTonemap)
	{
		VL_CORE_ASSERT(tonemapSettings == nullptr, "Can't have more than one tonemap settings inside a scene!");
		tonemapSettings = &m_CurrentScene->GetRegistry().get<Vulture::TonemapperSettingsComponent>(entity);
	}

	// No settings found, create one
	if (tonemapSettings == nullptr)
	{
		auto entity = m_CurrentScene->CreateEntity();
		tonemapSettings = &entity.AddComponent<Vulture::TonemapperSettingsComponent>();
	}

	m_Tonemapper.Run(tonemapSettings->Settings, Vulture::Renderer::GetCurrentCommandBuffer());

	if (m_OutputImage.GetLayout() != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		m_OutputImage.TransitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, Vulture::Renderer::GetCurrentCommandBuffer());
}

void PostProcessor::Resize(VkExtent2D newSize, Vulture::Image* inputImage)
{
	Vulture::Device::WaitIdle();
	m_InputImage = inputImage;
	m_ViewportSize = newSize;

	CreateImages();

	std::vector<int> data(m_OutputImage.GetImageSize().width * m_OutputImage.GetImageSize().height, 0);
	m_OutputImage.WritePixels(data.data());

	UpdateInputImage(inputImage);

	Vulture::Tonemap::CreateInfo tonemapInfo{};
	tonemapInfo.InputImage = &m_BloomImage;
	tonemapInfo.OutputImage = &m_OutputImage;

	m_Tonemapper.Init(tonemapInfo);
}

void PostProcessor::UpdateInputImage(Vulture::Image* inputImage)
{
	m_InputImage = inputImage;

	Vulture::Bloom::CreateInfo bloomInfo{};
	bloomInfo.InputImage = m_InputImage;
	bloomInfo.OutputImage = &m_BloomImage;
	m_Bloom.Init(bloomInfo);
}

void PostProcessor::CreateImages()
{
	{
		Vulture::Image::CreateInfo info{};
		info.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		info.Format = VK_FORMAT_R8G8B8A8_UNORM;
		info.Height = m_ViewportSize.height;
		info.Width = m_ViewportSize.width;
		info.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.Properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		info.SamplerInfo = Vulture::SamplerInfo{};
		info.DebugName = "Post Processor Output Image";
		m_OutputImage.Init(info);
	}
	{
		Vulture::Image::CreateInfo info{};
		info.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		info.Format = VK_FORMAT_R32G32B32A32_SFLOAT;
		info.Height = m_ViewportSize.height;
		info.Width = m_ViewportSize.width;
		info.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.Properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		info.SamplerInfo = Vulture::SamplerInfo{};
		info.DebugName = "Post Processor Output Image";
		m_BloomImage.Init(info);
	}
}
