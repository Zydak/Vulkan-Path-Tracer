#pragma once

#include "Vulture.h"
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

	void Init(Vulture::Image* inputImage);
	void Destroy();

	void SetScene(Vulture::Scene* currentScene);

	void Render();
	void Resize(VkExtent2D newSize, Vulture::Image* inputImage);
	void UpdateInputImage(Vulture::Image* inputImage);

	inline Vulture::Image* GetOutputImage() { return &m_OutputImage; }

private:
	// Initialization Stuff
	void CreateImages();

	Vulture::Scene* m_CurrentScene = nullptr;

	VkExtent2D m_ViewportSize = { 900, 900 };
	Vulture::Image* m_InputImage = nullptr;
	Vulture::Image  m_OutputImage;
	Vulture::Image  m_BloomImage;

	Vulture::Bloom m_Bloom;
	Vulture::Tonemap m_Tonemapper;
};