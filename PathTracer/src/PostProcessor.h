#pragma once

#include "Vulture.h"
#include "ImNodeFlow.h"

class PostProcessor
{
public:
	PostProcessor() = default;
	~PostProcessor() { BlackImage.Destroy(); } // a bit hacky, you'll only be able to have one PostProcessor

	void Init(Vulture::Image* inputImage);

	void Render();
	void RenderGraph();
	void Resize(VkExtent2D newSize, Vulture::Image* inputImage);

	inline ImFlow::ImNodeFlow* GetGridHandler() const { return m_Handler.get(); };
	inline Vulture::Image* GetOutputImage() { return &m_OutputImage; };


	static Vulture::Image BlackImage;

private:

	// Initialization Stuff
	void CreateImages();

	// ImGui Graph Stuff
	Vulture::Scope<ImFlow::ImNodeFlow> m_Handler;
	Vulture::FunctionQueue m_NodeQueue;

	VkExtent2D m_ViewportSize = { 900, 900 };
	Vulture::Image* m_InputImage;
	Vulture::Image  m_OutputImage;
};

//
// Nodes
//

class PathTracerOutputNode : public ImFlow::BaseNode
{
public:
	PathTracerOutputNode(Vulture::FunctionQueue* queue, Vulture::Image* pathTracerImage)
	{
		setTitle("Path Tracer Output");
		setStyle(ImFlow::NodeStyle::red());
		addOUT<Vulture::Image*>("Output")->behaviour([this]() { return m_PathTracerOutputImage; });

		m_PathTracerOutputImage = pathTracerImage;
		m_Queue = queue;
	}

	void UpdateImage(Vulture::Image* pathTracerImage)
	{
		m_PathTracerOutputImage = pathTracerImage;
	}

	void draw() override
	{

	}

private:
	Vulture::Image* m_PathTracerOutputImage;
	Vulture::FunctionQueue* m_Queue;
};

class OutputNode : public ImFlow::BaseNode
{
public:
	OutputNode(Vulture::FunctionQueue* queue, Vulture::Image* finalImage)
	{
		setTitle("Post Processor Output");
		setStyle(ImFlow::NodeStyle::red());
		addIN<Vulture::Image*>("Output", &PostProcessor::BlackImage, ImFlow::ConnectionFilter::SameType());

		m_OutputImage = finalImage;
		m_Queue = queue;
	}

	void UpdateImage(Vulture::Image* finalImage)
	{
		m_OutputImage = finalImage;
	}

	void draw() override
	{
		Vulture::Image* m_InputImage = getInVal<Vulture::Image*>("Output");
		if (m_InputImage == nullptr)
			return;

		auto func = [this](Vulture::Image* inputImage){

			auto lastLayout = inputImage->GetLayout();
			if (lastLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
				inputImage->TransitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Vulture::Renderer::GetCurrentCommandBuffer());

			m_OutputImage->TransitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, Vulture::Renderer::GetCurrentCommandBuffer());

			m_OutputImage->BlitImageToImage(
				inputImage,
				Vulture::Renderer::GetCurrentCommandBuffer()
			);

			if (lastLayout != VK_IMAGE_LAYOUT_UNDEFINED)
				inputImage->TransitionImageLayout(lastLayout, Vulture::Renderer::GetCurrentCommandBuffer());
		};
		
		m_Queue->PushTask(func, m_InputImage);
	}

private:
	Vulture::Image* m_OutputImage;
	Vulture::FunctionQueue* m_Queue;
};

class BloomNode : public ImFlow::BaseNode
{
public:
	BloomNode(Vulture::FunctionQueue* queue, VkExtent2D size)
	{
		setTitle("Bloom");
		setStyle(ImFlow::NodeStyle::green());
		addIN<Vulture::Image*>("Input", &PostProcessor::BlackImage, ImFlow::ConnectionFilter::SameType());
		addOUT<Vulture::Image*>("Output")->behaviour([this]() { return &m_OutputImage; });

		CreateImage(size);

		m_Queue = queue;

		Vulture::Bloom::CreateInfo info{};
		info.InputImage = const_cast<Vulture::Image*>(getInVal<Vulture::Image*>("Input"));
		info.OutputImage = &m_OutputImage;
		info.MipsCount = 6;

		m_Bloom.Init(info);
	}

	void draw() override
	{
		static VkImage prevHandle = getInVal<Vulture::Image*>("Input")->GetImage();
		m_CachedHandle = getInVal<Vulture::Image*>("Input")->GetImage();
		if (prevHandle != m_CachedHandle)
		{
			Vulture::Bloom::CreateInfo info{};
			info.InputImage = const_cast<Vulture::Image*>(getInVal<Vulture::Image*>("Input"));
			info.OutputImage = &m_OutputImage;

			m_Bloom.UpdateDescriptors(info);
			prevHandle = m_CachedHandle;
		}

		auto func = [this]() {
			m_Bloom.Run({}, Vulture::Renderer::GetCurrentCommandBuffer());
		};

		m_Queue->PushTask(func);
	}

	void CreateImage(VkExtent2D size)
	{
		bool gettingRecreated = m_OutputImage.IsInitialized();

		Vulture::Image::CreateInfo info{};
		info.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		info.Format = VK_FORMAT_R32G32B32A32_SFLOAT;
		info.Height = size.height;
		info.Width = size.width;
		info.LayerCount = 1;
		info.Tiling = VK_IMAGE_TILING_OPTIMAL;
		info.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.Properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		info.SamplerInfo = Vulture::SamplerInfo{};
		info.Type = Vulture::Image::ImageType::Image2D;
		info.DebugName = "Bloom Output Image";
		m_OutputImage.Init(info);

		if (gettingRecreated)
		{
			Vulture::Bloom::CreateInfo info{};
			info.InputImage = const_cast<Vulture::Image*>(getInVal<Vulture::Image*>("Input"));
			info.OutputImage = &m_OutputImage;

			m_Bloom.UpdateDescriptors(info);
		}
	}

private:

	Vulture::Image m_OutputImage;
	Vulture::Bloom m_Bloom;
	Vulture::FunctionQueue* m_Queue;

	VkImage m_CachedHandle = VK_NULL_HANDLE;
};