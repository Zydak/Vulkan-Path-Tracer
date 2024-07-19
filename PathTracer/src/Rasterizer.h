#pragma once

#include "Vulture.h"

struct PushConstantRasterizer
{
	glm::vec4 Color; // .a is lights count
	glm::mat4 Model;
};

struct LightsBufferEntry
{
	glm::vec4 Color;
	glm::vec3 Position;
};

class Rasterizer
{
public:
	Rasterizer() = default;
	~Rasterizer() { Destroy(); }

	struct CreateInfo
	{
		VkExtent2D Extent = { 0, 0 };
	};

	void Init(const CreateInfo& info);
	void Destroy();

	void Render(Vulture::Scene* scene);

	inline Vulture::Image* GetOutputImage() { return m_Framebuffer.GetImageNoVk(0).get(); }

public:

	// Render Parameters
	struct DrawInfo
	{

	};

	DrawInfo m_DrawInfo;
private:

	Vulture::Framebuffer m_Framebuffer; // Output image
	Vulture::Pipeline m_Pipeline;

	Vulture::PushConstant<PushConstantRasterizer> m_Push;
	Vulture::Buffer m_CameraBuffer;
	Vulture::DescriptorSet m_CameraSet;
	Vulture::Buffer m_LightsBuffer;
	Vulture::DescriptorSet m_LightsSet;
};