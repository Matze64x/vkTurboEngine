#pragma once

#include "vulkan/vulkan.hpp"
#include "vkte/vulkan_main_context.hpp"
#include "vkte/shader.hpp"
#include "vkte/shader_repository.hpp"

namespace vkte
{
class Pipeline
{
public:
	enum class BlendMode
	{
		None,
		Additive,
		AlphaBlend
	};

	struct GraphicsSettings
	{
		std::vector<vk::Format> color_formats;
		vk::Format depth_format = vk::Format::eUndefined;
		vk::SampleCountFlagBits rasterization_samples = vk::SampleCountFlagBits::e1;
		std::vector<Shader> shaders;
		vk::PolygonMode polygon_mode = vk::PolygonMode::eFill;
		vk::PrimitiveTopology primitive_topology = vk::PrimitiveTopology::eTriangleList;
		std::vector<vk::VertexInputBindingDescription> binding_descriptions;
		std::vector<vk::VertexInputAttributeDescription> attribute_description;
		std::vector<vk::PushConstantRange> pcrs;
		BlendMode blend_mode = BlendMode::None;
	};

	struct ComputeSettings
	{
		Shader shader;
		uint32_t push_constant_byte_size = 0;
	};

	explicit Pipeline(const VulkanMainContext& vmc);

	void construct(const GraphicsSettings& settings, const ShaderRepository& shader_repository, vk::DescriptorSetLayout* set_layout);
	void construct(const ComputeSettings& settings, const ShaderRepository& shader_repository, vk::DescriptorSetLayout* set_layout);
	void destruct();
	const vk::Pipeline& get() const;
	const vk::PipelineLayout& get_layout() const;

private:
	const VulkanMainContext& vmc;
	vk::PipelineLayout pipeline_layout;
	vk::Pipeline pipeline;
};
} // namespace vkte
