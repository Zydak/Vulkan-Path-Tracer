#pragma once

#include "VulkanHelper.h"
#include "ImNodeFlow.h"

class PostProcessor
{
public:
	PostProcessor() = default;
	~PostProcessor() = default;

	PostProcessor(const PostProcessor&) = delete;
	PostProcessor(PostProcessor&&) noexcept = delete;
	PostProcessor& operator=(const PostProcessor&) = delete;
	PostProcessor& operator=(PostProcessor&&) noexcept = delete;

	void Init(VulkanHelper::Image* inputImage);
	void Destroy();

	void SetScene(VulkanHelper::Scene* currentScene);

	void Render();
	void Resize(VkExtent2D newSize, VulkanHelper::Image* inputImage);
	void UpdateInputImage(VulkanHelper::Image* inputImage);

	inline VulkanHelper::Image* GetOutputImage() { return &m_OutputImage; }

private:
	// Initialization Stuff
	void CreateImages();

	VulkanHelper::Scene* m_CurrentScene = nullptr;

	VkExtent2D m_ViewportSize = { 900, 900 };
	VulkanHelper::Image* m_InputImage = nullptr;
	VulkanHelper::Image  m_OutputImage;
	VulkanHelper::Image  m_BloomImage;

	VulkanHelper::Bloom m_Bloom;
	VulkanHelper::Tonemap m_Tonemapper;
};