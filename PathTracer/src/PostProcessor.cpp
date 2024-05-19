#include "PostProcessor.h"

void PostProcessor::Init(Vulture::Image* inputImage)
{
	static bool blackMade = false;
	if (!blackMade)
	{
		Vulture::Image::CreateInfo info{};
		info.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		info.Format = VK_FORMAT_R8G8B8A8_UNORM;
		info.Height = 1;
		info.Width = 1;
		info.LayerCount = 1;
		info.Tiling = VK_IMAGE_TILING_OPTIMAL;
		info.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.Properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		info.SamplerInfo = Vulture::SamplerInfo{};
		info.Type = Vulture::Image::ImageType::Image2D;
		info.DebugName = "Post Processor Black Image";
		m_OutputImage.Init(info);
		m_OutputImage.TransitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		BlackImage.Init(info);
		blackMade = true;
	}

	m_InputImage = inputImage;

	m_Handler = std::make_unique<ImFlow::ImNodeFlow>("Main");

	std::shared_ptr<OutputNode> outputNode = m_Handler->addNode<OutputNode>({ 0, 0 }, &m_NodeQueue, &m_OutputImage);
	std::shared_ptr<PathTracerOutputNode> inputNode = m_Handler->addNode<PathTracerOutputNode>({ 0, 0 }, &m_NodeQueue, m_InputImage);
	std::shared_ptr<BloomNode> bloomNode = m_Handler->addNode<BloomNode>({ 0, 0 }, &m_NodeQueue, VkExtent2D{ 900, 900 });

	CreateImages();

	outputNode->inPin("Output")->createLink(inputNode->outPin("Output"));
}

void PostProcessor::Render()
{
	m_NodeQueue.RunTasks();

	if (m_OutputImage.GetLayout() != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		m_OutputImage.TransitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, Vulture::Renderer::GetCurrentCommandBuffer());
}

void PostProcessor::RenderGraph()
{
	ImGui::Begin("Post Processing Graph");

	m_Handler->update();

	ImGui::End();
}

void PostProcessor::Resize(VkExtent2D newSize, Vulture::Image* inputImage)
{
	Vulture::Device::WaitIdle();
	m_InputImage = inputImage;
	m_ViewportSize = newSize;
	auto* queue = m_NodeQueue.GetQueue();
	while (queue->size() != 0)
	{
		queue->pop();
	}

	CreateImages();

	std::vector<int> data(m_OutputImage.GetImageSize().width * m_OutputImage.GetImageSize().height, 0);
	m_OutputImage.WritePixels(data.data());
}

void PostProcessor::CreateImages()
{
	{
		Vulture::Image::CreateInfo info{};
		info.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		info.Format = VK_FORMAT_R8G8B8A8_UNORM;
		info.Height = m_ViewportSize.height;
		info.Width = m_ViewportSize.width;
		info.LayerCount = 1;
		info.Tiling = VK_IMAGE_TILING_OPTIMAL;
		info.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.Properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		info.SamplerInfo = Vulture::SamplerInfo{};
		info.Type = Vulture::Image::ImageType::Image2D;
		info.DebugName = "Post Processor Output Image";
		m_OutputImage.Init(info);
		m_OutputImage.TransitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
}

Vulture::Image PostProcessor::BlackImage;

