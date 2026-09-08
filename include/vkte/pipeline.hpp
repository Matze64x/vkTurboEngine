#pragma once

#include "vulkan/vulkan.hpp"
#include "vkte/vulkan_main_context.hpp"
#include "vkte/shader.hpp"
#include <memory>

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

	enum class Type
	{
		Graphics,
		Compute
	};

	Pipeline(const VulkanMainContext& vmc, Type type);
	Pipeline(const VulkanMainContext& vmc, const GraphicsSettings& settings);
	Pipeline(const VulkanMainContext& vmc, const ComputeSettings& settings);
	GraphicsSettings& get_graphics_settings();
	ComputeSettings& get_compute_settings();

	bool compile_shaders();
	void construct(vk::DescriptorSetLayout* set_layout);
	void reconstruct(vk::DescriptorSetLayout* set_layout);
	void destruct();
	const vk::Pipeline& get() const;
	const vk::PipelineLayout& get_layout() const;

private:
	Type type;
	std::unique_ptr<GraphicsSettings> graphics_settings;
	std::unique_ptr<ComputeSettings> compute_settings;

	const VulkanMainContext& vmc;
	vk::PipelineLayout pipeline_layout;
	vk::Pipeline pipeline;
	std::vector<vk::PipelineShaderStageCreateInfo> shader_stages;
	std::vector<vk::SpecializationInfo> spec_infos;
};
} // namespace vkte
