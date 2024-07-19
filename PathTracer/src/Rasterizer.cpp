#include "pch.h"

#include "Rasterizer.h"
#include "Components.h"

struct CameraUBO
{
	glm::mat4 ProjView;
	glm::mat4 ViewInverse;
	glm::mat4 ProjInverse;
};

void Rasterizer::Init(const CreateInfo& info)
{
	// Push constant
	m_Push.Init({ VK_SHADER_STAGE_VERTEX_BIT });

	// Render pass
	Vulture::Framebuffer::CreateInfo framebufferInfo{};
	framebufferInfo.AttachmentsFormats = { Vulture::FramebufferAttachment::ColorRGBA8, Vulture::FramebufferAttachment::Depth16 };
	framebufferInfo.Extent = info.Extent;
	Vulture::Framebuffer::RenderPassCreateInfo renderpassInfo{};
	framebufferInfo.RenderPassInfo = &renderpassInfo;

	m_Framebuffer.Init(framebufferInfo);

	// Lights UBO
	{
		Vulture::Buffer::CreateInfo bufferInfo{};
		bufferInfo.InstanceCount = 1;
		bufferInfo.InstanceSize = sizeof(LightsBufferEntry) * 100;
		bufferInfo.MemoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		bufferInfo.UsageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		m_LightsBuffer.Init(bufferInfo);

		// Lights Descriptor Set
		m_LightsSet.Init(&Vulture::Renderer::GetDescriptorPool(), { Vulture::DescriptorSetLayout::Binding{0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT} });
		m_LightsSet.AddBuffer(0, m_LightsBuffer.DescriptorInfo());
		m_LightsSet.Build();
	}

	// Camera UBO
	{
		Vulture::Buffer::CreateInfo bufferInfo{};
		bufferInfo.InstanceCount = 1;
		bufferInfo.InstanceSize = sizeof(CameraUBO);
		bufferInfo.MemoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		bufferInfo.UsageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		m_CameraBuffer.Init(bufferInfo);

		// Camera Descriptor Set
		m_CameraSet.Init(&Vulture::Renderer::GetDescriptorPool(), { Vulture::DescriptorSetLayout::Binding{0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT} });
		m_CameraSet.AddBuffer(0, m_CameraBuffer.DescriptorInfo());
		m_CameraSet.Build();
	}

	// Pipeline
	Vulture::Pipeline::GraphicsCreateInfo pipelineInfo{};
	Vulture::Shader vertex({ "src/shaders/rasterizer.vert", VK_SHADER_STAGE_VERTEX_BIT });
	Vulture::Shader fragment({ "src/shaders/rasterizer.frag", VK_SHADER_STAGE_FRAGMENT_BIT });

	pipelineInfo.Shaders = { &vertex, &fragment };
	pipelineInfo.DescriptorSetLayouts = { m_CameraSet.GetDescriptorSetLayout()->GetDescriptorSetLayoutHandle(), m_LightsSet.GetDescriptorSetLayout()->GetDescriptorSetLayoutHandle() };

	pipelineInfo.AttributeDesc = Vulture::Mesh::Vertex::GetAttributeDescriptions();
	pipelineInfo.BindingDesc = Vulture::Mesh::Vertex::GetBindingDescriptions();
	pipelineInfo.BlendingEnable = false;
	pipelineInfo.ColorAttachmentCount = 1;
	pipelineInfo.CullMode = VK_CULL_MODE_BACK_BIT;
	pipelineInfo.debugName = "Rasterizer pipeline";
	pipelineInfo.DepthClamp = false;
	pipelineInfo.DepthTestEnable = true;
	pipelineInfo.Height = info.Extent.height;
	pipelineInfo.Width = info.Extent.width;
	pipelineInfo.PolygonMode = VK_POLYGON_MODE_FILL;
	pipelineInfo.PushConstants = m_Push.GetRangePtr();
	pipelineInfo.RenderPass = m_Framebuffer.GetRenderPass();
	pipelineInfo.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	m_Pipeline.Init(pipelineInfo);
}

void Rasterizer::Destroy()
{

}

void Rasterizer::Render(Vulture::Scene* scene)
{
	auto modelView = scene->GetRegistry().view<ModelComponent, TransformComponent>();

	std::vector<LightsBufferEntry> lights;
	int lightsCount = 0;

	// Prepare lights buffer
	for (auto& entity : modelView)
	{
		auto& [modelComp, TransformComp] = scene->GetRegistry().get<ModelComponent, TransformComponent>(entity);

		Vulture::Model* model = modelComp.ModelHandle.GetModel();
		std::vector<Vulture::Ref<Vulture::Mesh>> meshes = model->GetMeshes();
		for (uint32_t i = 0; i < model->GetMeshCount(); i++)
		{
			Vulture::Material mat = model->GetMaterial(i);

			if (mat.EmissiveColor.x != 0.0f && mat.EmissiveColor.y != 0.0f && mat.EmissiveColor.z != 0 && mat.EmissiveColor.a != 0)
			{
				glm::mat4 modelMat = model->GetMatrix(i) * TransformComp.Transform.GetMat4();
				glm::vec3 pos(modelMat[0][3], modelMat[1][3], modelMat[2][3]);
				lights.push_back({ mat.EmissiveColor, pos });
				lightsCount++;
			}
		}
	}

	if (!lights.empty())
		m_LightsBuffer.WriteToBuffer(lights.data());

	// It should be checked whether we have a perspective or orthographic camera in the scene but who cares I'm not gonna use ortho anyway
	Vulture::PerspectiveCamera* cam = &PerspectiveCameraComponent::GetMainCamera(scene)->Camera;

	CameraUBO cameraData;
	cameraData.ProjView = cam->ProjMat * cam->ViewMat;
	cameraData.ViewInverse = glm::inverse(cam->ViewMat);
	cameraData.ProjInverse = glm::inverse(cam->ProjMat);

	m_CameraBuffer.WriteToBuffer(&cameraData);

	VkCommandBuffer cmd = Vulture::Renderer::GetCurrentCommandBuffer();
	std::vector<VkClearValue> clear = {
		{ 0.1f, 0.1f, 0.1f, 1.0f },
		{ 1, 0 }
	};

	m_Framebuffer.Bind(cmd, clear);

	m_Pipeline.Bind(cmd);
	m_CameraSet.Bind(0, m_Pipeline.GetPipelineLayout(), VK_PIPELINE_BIND_POINT_GRAPHICS, cmd);
	m_LightsSet.Bind(1, m_Pipeline.GetPipelineLayout(), VK_PIPELINE_BIND_POINT_GRAPHICS, cmd);

	for (auto& entity : modelView)
	{
		auto& [modelComp, TransformComp] = scene->GetRegistry().get<ModelComponent, TransformComponent>(entity);

		Vulture::Model* model = modelComp.ModelHandle.GetModel();
		std::vector<Vulture::Ref<Vulture::Mesh>> meshes = model->GetMeshes();
		std::vector<Vulture::Ref<Vulture::DescriptorSet>> sets = model->GetDescriptors();
		for (uint32_t i = 0; i < model->GetMeshCount(); i++)
		{
			m_Push.GetDataPtr()->Color = glm::vec4(glm::vec3(model->GetMaterial(i).Color), lightsCount);
			m_Push.GetDataPtr()->Model = TransformComp.Transform.GetMat4();

			m_Push.Push(m_Pipeline.GetPipelineLayout(), Vulture::Renderer::GetCurrentCommandBuffer());

			//sets[i]->Bind(1, m_Pipeline.GetPipelineLayout(), VK_PIPELINE_BIND_POINT_GRAPHICS, Vulture::Renderer::GetCurrentCommandBuffer());
			meshes[i]->Bind(Vulture::Renderer::GetCurrentCommandBuffer());
			meshes[i]->Draw(Vulture::Renderer::GetCurrentCommandBuffer(), 1, 0);
		}
	}

	m_Framebuffer.Unbind(cmd);
}