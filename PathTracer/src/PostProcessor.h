#pragma once

#include "Vulture.h"
#include "ImNodeFlow.h"

#define CENTER_WIDGET(pos, width)\
ImGui::Text("");\
ImGui::SameLine(pos);\
ImGui::SetNextItemWidth(width);\

struct NodeOutput
{
	NodeOutput() = default;
	NodeOutput(Vulture::Image* image) { Image = image; }

	void Init(Vulture::Image* image) 
	{ 
		Image = image; 

		Queue.Clear();
	};

	Vulture::Image* Image;
	Vulture::FunctionQueue Queue;
};

class BaseNodeVul
{
public:
	// 	virtual std::function<void()> GetFunc() = 0;
	// 	virtual Vulture::Image* GetOutputImage() = 0;
	bool PinsEvaluated() { return m_PinsEvaluated; };
	void SetPinsEvaluated(bool val) { m_PinsEvaluated = val; };

private:
	bool m_PinsEvaluated = false;
};

class OutputNode;
class PathTracerOutputNode;

class PostProcessor
{
public:
	PostProcessor() = default;
	~PostProcessor() { BlackImage.Destroy(); } // TODO: a bit hacky, you'll only be able to have one PostProcessor

	void Init(Vulture::Image* inputImage);

	void Evaluate();
	void Render();
	void RenderGraph();
	void Resize(VkExtent2D newSize, Vulture::Image* inputImage);
	void EndFrame();

	inline ImFlow::ImNodeFlow* GetGridHandler() const { return m_Handler.get(); };
	inline Vulture::Image* GetOutputImage() { return &m_OutputImage; };

	static Vulture::Image BlackImage;
	static NodeOutput EmptyNodeOutput;

	static bool NodeDeleted;

private:

	// Initialization Stuff
	void CreateImages();

	// ImGui Graph Stuff
	ImFlow::BaseNode* SpawnFunction();
	Vulture::Scope<ImFlow::ImNodeFlow> m_Handler;
	Vulture::FunctionQueue m_NodeQueue;
	std::shared_ptr<OutputNode> m_OutputNode;
	std::shared_ptr<PathTracerOutputNode> m_InputNode;

	VkExtent2D m_ViewportSize = { 900, 900 };
	Vulture::Image* m_InputImage;
	Vulture::Image  m_OutputImage;

	std::vector<std::shared_ptr<ImFlow::BaseNode>> m_NodesRef;
};

// TODO: Move nodes to a separate file
//
// Nodes
//

class PathTracerOutputNode : public ImFlow::BaseNode, public BaseNodeVul
{
public:
	PathTracerOutputNode(Vulture::Image* pathTracerImage)
	{
		setTitle("Path Tracer Output");
		setStyle(ImFlow::NodeStyle::red());
		addOUT<NodeOutput>("Output")->behaviour(
			[this]() 
			{
				auto ins = this->getIns();

				for (int i = 0; i < ins.size(); i++)
				{
					auto link = ins[i]->getLink().lock();
					if (link == nullptr)
						continue;

					auto leftPin = link->left();
					BaseNodeVul* leftNode = dynamic_cast<BaseNodeVul*>(leftPin->getParent());
					if (!leftNode->PinsEvaluated())
					{
						leftNode->SetPinsEvaluated(true);
						leftPin->resolve();
					}
				}

				m_NodeOutput.Init(m_PathTracerOutputImage);
				return m_NodeOutput;
			});

		m_PathTracerOutputImage = pathTracerImage;
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
	NodeOutput m_NodeOutput;
};

class OutputNode : public ImFlow::BaseNode, public  BaseNodeVul
{
public:
	OutputNode(Vulture::FunctionQueue* queue, Vulture::Image* finalImage)
	{
		setTitle("Post Processor Output");
		setStyle(ImFlow::NodeStyle::red());
		addIN<NodeOutput>("Window Output", PostProcessor::EmptyNodeOutput, ImFlow::ConnectionFilter::SameType());

		m_OutputImage = finalImage;
		m_Queue = queue;
	}

	void UpdateImage(Vulture::Image* finalImage)
	{
		m_OutputImage = finalImage;
	}

	void Resolve()
	{
		if (PinsEvaluated())
			return;
		else
		{
			m_NodeOutput = {};
			SetPinsEvaluated(true);
		}

		auto ins = this->getIns();

		for (int i = 0; i < ins.size(); i++)
		{
			auto link = ins[i]->getLink().lock();
			if (link == nullptr)
				continue;

			auto leftPin = link->left();
			if (leftPin == nullptr)
				continue;
			leftPin->resolve();
		}

		m_NodeOutput = getInVal<NodeOutput>("Window Output");

		auto func = [this](Vulture::Image* inputImage) {

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

		m_NodeOutput.Queue.PushTask(func, m_NodeOutput.Image);

		*m_Queue = std::move(m_NodeOutput.Queue);
	}

	void draw() override
	{
		
	}

private:
	Vulture::Image* m_OutputImage;
	Vulture::FunctionQueue* m_Queue;
	NodeOutput m_NodeOutput{};
};

class BloomNode : public ImFlow::BaseNode, public  BaseNodeVul
{
public:
	BloomNode(VkExtent2D size)
	{
		setTitle("Bloom");
		setStyle(ImFlow::NodeStyle::green());
		addIN<NodeOutput>("Input", PostProcessor::EmptyNodeOutput, ImFlow::ConnectionFilter::SameType());
		addOUT<NodeOutput>("Output")->behaviour(
			[this]() 
			{
				if (PinsEvaluated())
					return m_NodeOutput;
				else
				{
					m_NodeOutput = {};
					SetPinsEvaluated(true);
				}

				auto ins = this->getIns();

				for (int i = 0; i < ins.size(); i++)
				{
					auto link = ins[i]->getLink().lock();
					if (link == nullptr)
						continue;

					auto leftPin = link->left();
					leftPin->resolve();
				}

				auto func = [this](Vulture::Image* inputImage) {
					m_CachedHandle = inputImage->GetImage();
					if (m_PrevHandle != m_CachedHandle)
					{
						Vulture::Bloom::CreateInfo info{};
						info.MipsCount = m_RunInfo.MipCount;
						info.InputImage = const_cast<Vulture::Image*>(inputImage);
						info.OutputImage = &m_OutputImage;

						m_Bloom.Init(info);
						m_PrevHandle = m_CachedHandle;
					}

					m_Bloom.Run(m_RunInfo, Vulture::Renderer::GetCurrentCommandBuffer());
				};

				m_NodeOutput = getInVal<NodeOutput>("Input");
				m_NodeOutput.Queue.PushTask(func, m_NodeOutput.Image);
				m_NodeOutput.Image = &m_OutputImage;

				return m_NodeOutput;
			}
		);

		CreateImage(size);
	}

	void draw() override
	{
		int prevMipCount = m_RunInfo.MipCount;

		// ImGui
		ImGui::Text("");
		CENTER_WIDGET(-50, 50);
		ImGui::SliderFloat("Threshold", &m_RunInfo.Threshold, 0.0f, 3.0f);
		CENTER_WIDGET(-50, 50);
		ImGui::SliderFloat("Strength", &m_RunInfo.Strength, 0.0f, 2.0f);
		CENTER_WIDGET(-50, 50);
		ImGui::SliderInt("MipCount", &m_RunInfo.MipCount, 1, 10);

		if (prevMipCount != m_RunInfo.MipCount)
		{
			m_Bloom.RecreateDescriptors(m_RunInfo.MipCount);
		}
	}

	void CreateImage(VkExtent2D size)
	{
		Vulture::Image::CreateInfo info{};
		info.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		info.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
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
	}

private:
	Vulture::Image m_OutputImage;
	Vulture::Bloom m_Bloom;
	Vulture::Bloom::BloomInfo m_RunInfo{};
	NodeOutput m_NodeOutput{};

	VkImage m_CachedHandle = VK_NULL_HANDLE;
	VkImage m_PrevHandle = VK_NULL_HANDLE;
};

class TonemapNode : public ImFlow::BaseNode, public  BaseNodeVul
{
public:
	TonemapNode(VkExtent2D size)
	{
		setTitle("Tonemap");
		setStyle(ImFlow::NodeStyle::green());
		addIN<NodeOutput>("Input", PostProcessor::EmptyNodeOutput, ImFlow::ConnectionFilter::SameType());
		addOUT<NodeOutput>("Output")->behaviour(
			[this]()
			{
				if (PinsEvaluated()) // Node was already evaluated so just return node output
					return m_NodeOutput;
				else // Node is evaluated for the first time in the frame so reset m_NodeOutput from prev frame and continue
				{
					m_NodeOutput = {};
					SetPinsEvaluated(true);
				}

				auto ins = this->getIns();

				for (int i = 0; i < ins.size(); i++)
				{
					auto link = ins[i]->getLink().lock();
					if (link == nullptr)
						continue;

					auto leftPin = link->left();
					leftPin->resolve();
				}

				auto func = [this](Vulture::Image* inputImage) 
				{
					m_CachedHandle = inputImage->GetImage();
					if (m_PrevHandle != m_CachedHandle)
					{
						Vulture::Tonemap::CreateInfo info{};
						info.InputImage = inputImage;
						info.OutputImage = &m_OutputImage;

						m_Tonemap.Init(info);
						m_PrevHandle = m_CachedHandle;
					}

					m_Tonemap.Run(m_RunInfo, Vulture::Renderer::GetCurrentCommandBuffer());
				};

				m_NodeOutput = getInVal<NodeOutput>("Input");
				m_NodeOutput.Queue.PushTask(func, m_NodeOutput.Image);
				m_NodeOutput.Image = &m_OutputImage;

				return m_NodeOutput;
			}
		);

		CreateImage(size);

		NodeOutput nodeInput = getInVal<NodeOutput>("Input");
	}

	void draw() override
	{
		ImGui::Text("");
		CENTER_WIDGET(-50, 50);
		ImGui::SliderFloat("Exposure", &m_RunInfo.Exposure, 0.0f, 3.0f);
		CENTER_WIDGET(-50, 50);
		ImGui::SliderFloat("Contrast", &m_RunInfo.Contrast, 0.0f, 3.0f);
		CENTER_WIDGET(-50, 50);
		ImGui::SliderFloat("Brightness", &m_RunInfo.Brightness, 0.0f, 3.0f);
		CENTER_WIDGET(-50, 50);
		ImGui::SliderFloat("Saturation", &m_RunInfo.Saturation, 0.0f, 3.0f);
		CENTER_WIDGET(-50, 50);
		ImGui::SliderFloat("Vignette", &m_RunInfo.Vignette, 0.0f, 1.0f);
		CENTER_WIDGET(-50, 50);
		ImGui::SliderFloat("Gamma", &m_RunInfo.Gamma, 0.0f, 2.0f);
		CENTER_WIDGET(-50, 50);
		ImGui::SliderFloat("Temperature", &m_RunInfo.Temperature, -1.0f, 1.0f);
		CENTER_WIDGET(-50, 50);
		ImGui::SliderFloat("Tint", &m_RunInfo.Tint, -1.0f, 1.0f);
		CENTER_WIDGET(-50, 150);
		ImGui::ColorEdit3("Color Filter", (float*)&m_RunInfo.ColorFilter);

		CENTER_WIDGET(-50, 50);
		if (ImGui::Checkbox("Chromatic Aberration", &m_UseChromaticAberration)) 
		{ 
			m_Tonemap.RecompileShader(m_Tonemapper, m_UseChromaticAberration); 
		}

		if (m_UseChromaticAberration)
		{
			CENTER_WIDGET(-50, 50);
			ImGui::SliderInt2("Aberration Offset R", (int*)&m_RunInfo.AberrationOffsets[0], -5, 5);
			CENTER_WIDGET(-50, 50);
			ImGui::SliderInt2("Aberration Offset G", (int*)&m_RunInfo.AberrationOffsets[1], -5, 5);
			CENTER_WIDGET(-50, 50);
			ImGui::SliderInt2("Aberration Offset B", (int*)&m_RunInfo.AberrationOffsets[2], -5, 5);

			CENTER_WIDGET(-50, 50);
			ImGui::SliderFloat("Aberration Vignette", &m_RunInfo.AberrationVignette, 0.0f, 10.0f);
		}

		CENTER_WIDGET(-50, 50);
		ImGui::Text("Tonemappers");
		static int currentTonemapper = 0;
		const char* tonemappers[] = { "Filmic", "Hill Aces", "Narkowicz Aces", "Exposure Mapping", "Uncharted 2", "Reinchard Extended" };

		CENTER_WIDGET(-50, 200);
		if (ImGui::ListBox("", &currentTonemapper, tonemappers, IM_ARRAYSIZE(tonemappers), IM_ARRAYSIZE(tonemappers)))
		{
			m_Tonemapper = (Vulture::Tonemap::Tonemappers)currentTonemapper;
			m_Tonemap.RecompileShader(m_Tonemapper, m_UseChromaticAberration);
		}

		if (currentTonemapper == Vulture::Tonemap::Tonemappers::ReinchardExtended)
		{
			CENTER_WIDGET(-50, 50);
			ImGui::SliderFloat("White Point", &m_RunInfo.whitePointReinhard, 0.0f, 5.0f);
		}
	}

	void CreateImage(VkExtent2D size)
	{
		bool gettingRecreated = m_OutputImage.IsInitialized();

		Vulture::Image::CreateInfo info{};
		info.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		info.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
		info.Height = size.height;
		info.Width = size.width;
		info.LayerCount = 1;
		info.Tiling = VK_IMAGE_TILING_OPTIMAL;
		info.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.Properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		info.SamplerInfo = Vulture::SamplerInfo{};
		info.Type = Vulture::Image::ImageType::Image2D;
		info.DebugName = "Tonemap Output Image";
		m_OutputImage.Init(info);

		if (gettingRecreated)
		{
			NodeOutput nodeInput = getInVal<NodeOutput>("Input");
			
			Vulture::Tonemap::CreateInfo info{};
			info.InputImage = const_cast<Vulture::Image*>(nodeInput.Image);
			info.OutputImage = &m_OutputImage;

			m_Tonemap.Init(info);
		}
	}

private:

	Vulture::Image m_OutputImage;
	Vulture::Tonemap m_Tonemap;
	Vulture::Tonemap::TonemapInfo m_RunInfo{};
	Vulture::Tonemap::Tonemappers m_Tonemapper = Vulture::Tonemap::Tonemappers::Filmic;
	bool m_UseChromaticAberration = false;
	NodeOutput m_NodeOutput{};

	VkImage m_CachedHandle = VK_NULL_HANDLE;
	VkImage m_PrevHandle = VK_NULL_HANDLE;
};