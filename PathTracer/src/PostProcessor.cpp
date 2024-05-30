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

		EmptyNodeOutput.Init(&BlackImage);
	}

	m_InputImage = inputImage;

	m_Handler = std::make_unique<ImFlow::ImNodeFlow>("Main");

	CreateImages();

	m_InputNode = m_Handler->addNode<PathTracerOutputNode>({ 0, 400 }, m_InputImage);
	std::shared_ptr<BloomNode> bloomNode = m_Handler->addNode<BloomNode>({ 170, 400 }, VkExtent2D{ 900, 900 });
	m_NodesRef.push_back(bloomNode);
	std::shared_ptr<TonemapNode> tonemapNode = m_Handler->addNode<TonemapNode>({ 370, 400 }, VkExtent2D{ 900, 900 });
	m_NodesRef.push_back(tonemapNode);
	m_OutputNode = m_Handler->addNode<OutputNode>({ 600, 400 }, &m_NodeQueue, &m_OutputImage);

	bloomNode->inPin("Input")->createLink(m_InputNode->outPin("Output"));
	tonemapNode->inPin("Input")->createLink(bloomNode->outPin("Output"));
	m_OutputNode->inPin("Window Output")->createLink(tonemapNode->outPin("Output"));
}

void PostProcessor::Evaluate()
{
	m_OutputNode->Resolve();
}

void PostProcessor::Render()
{
	if (!NodeDeleted)
		m_NodeQueue.RunTasks();
	else
		NodeDeleted = false;
	m_NodeQueue.Clear();

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
	m_NodeQueue.Clear();

	CreateImages();

	std::vector<int> data(m_OutputImage.GetImageSize().width * m_OutputImage.GetImageSize().height, 0);
	m_OutputImage.WritePixels(data.data());
}

void PostProcessor::EndFrame()
{
	Vulture::Device::WaitIdle();

	for (int i = 0; i < m_NodesRef.size(); i++)
	{
		if (m_NodesRef[i]->toDestroy())
			m_NodesRef.erase(m_NodesRef.begin() + i);
	}

	// Check if user tried to delete output or input node node (they shouldn't be deletable)
	if (m_OutputNode->toDestroy())
	{
		m_OutputNode = m_Handler->addNode<OutputNode>(m_OutputNode->getPos(), &m_NodeQueue, &m_OutputImage);
	}
	if (m_InputNode->toDestroy())
	{
		m_InputNode = m_Handler->addNode<PathTracerOutputNode>(m_InputNode->getPos(), m_InputImage);
	}

	// Reset all nodes
	ImFlow::ImNodeFlow* handler = GetGridHandler();
	for (auto& node : handler->getNodes())
	{
		// Update Input Nodes
		{
			PathTracerOutputNode* inputNode = dynamic_cast<PathTracerOutputNode*>(node.second.get());
			if (inputNode != nullptr)
			{
				inputNode->SetPinsEvaluated(false);
			}
		}

		// Update Output Nodes
		{
			OutputNode* outputNode = dynamic_cast<OutputNode*>(node.second.get());
			if (outputNode != nullptr)
			{
				outputNode->SetPinsEvaluated(false);
			}
		}

		// Update Bloom Nodes
		{
			BloomNode* bloomNode = dynamic_cast<BloomNode*>(node.second.get());
			if (bloomNode != nullptr)
			{
				bloomNode->SetPinsEvaluated(false);
			}
		}

		// Update Tonemap Nodes
		{
			TonemapNode* tonemapNode = dynamic_cast<TonemapNode*>(node.second.get());
			if (tonemapNode != nullptr)
			{
				tonemapNode->SetPinsEvaluated(false);
			}
		}
	}
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

NodeOutput PostProcessor::EmptyNodeOutput;

bool PostProcessor::NodeDeleted = false;

