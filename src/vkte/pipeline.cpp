#include "vkte/pipeline.hpp"

#include <array>
#include "vkte/vkte_log.hpp"

namespace vkte
{
Pipeline::Pipeline(const VulkanMainContext& vmc) : vmc(vmc)
{}

static vk::ShaderCreateInfoEXT make_shader_create_info(const ShaderRepository::CompiledShader& compiled, const Shader& shader, vk::ShaderStageFlagBits next_stage, const std::vector<vk::DescriptorSetLayout>& set_layouts, const std::vector<vk::PushConstantRange>& pcrs)
{
	vk::ShaderCreateInfoEXT sci;
	sci.stage = shader.stage_flag;
	sci.nextStage = next_stage;
	sci.codeType = vk::ShaderCodeTypeEXT::eSpirv;
	sci.codeSize = compiled.spirv.size() * sizeof(uint32_t);
	sci.pCode = compiled.spirv.data();
	sci.pName = shader.entry_point.c_str();
	sci.setLayoutCount = set_layouts.size();
	sci.pSetLayouts = set_layouts.data();
	sci.pushConstantRangeCount = pcrs.size();
	sci.pPushConstantRanges = pcrs.data();
	sci.pSpecializationInfo = &compiled.specialization_info;
	return sci;
}

void Pipeline::construct(const GraphicsSettings& settings, const ShaderRepository& shader_repository, const std::vector<vk::DescriptorSetLayout>& set_layouts)
{
	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = set_layouts.size();
	plci.pSetLayouts = set_layouts.data();
	plci.pushConstantRangeCount = settings.pcrs.size();
	plci.pPushConstantRanges = settings.pcrs.data();
	pipeline_layout = vmc.logical_device.get().createPipelineLayout(plci);

	stages.clear();
	for (const Shader& shader : settings.shaders) stages.push_back(shader.stage_flag);

	std::vector<vk::ShaderCreateInfoEXT> create_infos;
	create_infos.reserve(settings.shaders.size());
	for (size_t i = 0; i < settings.shaders.size(); i++)
	{
		const vk::ShaderStageFlagBits next_stage = i + 1 < settings.shaders.size() ? settings.shaders[i + 1].stage_flag : vk::ShaderStageFlagBits{};
		create_infos.push_back(make_shader_create_info(shader_repository.get_compiled_shader(settings.shaders[i]), settings.shaders[i], next_stage, set_layouts, settings.pcrs));
	}
	vk::ResultValue<std::vector<vk::ShaderEXT>> shaders_result = vmc.logical_device.get().createShadersEXT(create_infos);
	VKTE_CHECK(shaders_result.result, "Failed to create shader objects!");
	shader_objects = std::move(shaders_result.value);

	GraphicsState state;
	for (const vk::VertexInputBindingDescription& binding : settings.binding_descriptions) state.vertex_bindings.emplace_back(binding.binding, binding.stride, binding.inputRate, 1);
	for (const vk::VertexInputAttributeDescription& attribute : settings.attribute_description) state.vertex_attributes.emplace_back(attribute.location, attribute.binding, attribute.format, attribute.offset);

	state.primitive_topology = settings.primitive_topology;
	state.polygon_mode = settings.polygon_mode;
	state.rasterization_samples = settings.rasterization_samples;
	state.depth_write_enable = settings.blend_mode == BlendMode::None;

	vk::ColorBlendEquationEXT equation;
	vk::Bool32 blend_enable = VK_FALSE;
	switch (settings.blend_mode)
	{
	case BlendMode::Additive:
		blend_enable = VK_TRUE;
		equation.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		equation.dstColorBlendFactor = vk::BlendFactor::eOne;
		break;
	case BlendMode::AlphaBlend:
		blend_enable = VK_TRUE;
		equation.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		equation.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		break;
	case BlendMode::None:
	default:
		equation.srcColorBlendFactor = vk::BlendFactor::eOne;
		equation.dstColorBlendFactor = vk::BlendFactor::eZero;
		break;
	}
	equation.colorBlendOp = vk::BlendOp::eAdd;
	equation.srcAlphaBlendFactor = vk::BlendFactor::eOne;
	equation.dstAlphaBlendFactor = vk::BlendFactor::eZero;
	equation.alphaBlendOp = vk::BlendOp::eAdd;

	const uint32_t attachment_count = uint32_t(settings.color_formats.size());
	state.color_blend_enable.assign(attachment_count, blend_enable);
	state.color_blend_equation.assign(attachment_count, equation);
	state.color_write_mask.assign(attachment_count, vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	graphics_state = std::move(state);

	bind_point = vk::PipelineBindPoint::eGraphics;
}

void Pipeline::construct(const ComputeSettings& settings, const ShaderRepository& shader_repository, const std::vector<vk::DescriptorSetLayout>& set_layouts)
{
	std::vector<vk::PushConstantRange> pcrs;
	if (settings.push_constant_byte_size > 0) pcrs.emplace_back(vk::ShaderStageFlagBits::eCompute, 0, settings.push_constant_byte_size);

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = set_layouts.size();
	plci.pSetLayouts = set_layouts.data();
	plci.pushConstantRangeCount = pcrs.size();
	plci.pPushConstantRanges = pcrs.data();
	pipeline_layout = vmc.logical_device.get().createPipelineLayout(plci);

	stages = {vk::ShaderStageFlagBits::eCompute};
	const vk::ShaderCreateInfoEXT create_info = make_shader_create_info(shader_repository.get_compiled_shader(settings.shader), settings.shader, vk::ShaderStageFlagBits{}, set_layouts, pcrs);
	vk::ResultValue<std::vector<vk::ShaderEXT>> shaders_result = vmc.logical_device.get().createShadersEXT(create_info);
	VKTE_CHECK(shaders_result.result, "Failed to create shader object!");
	shader_objects = std::move(shaders_result.value);
	graphics_state.reset();

	bind_point = vk::PipelineBindPoint::eCompute;
}

void Pipeline::destruct()
{
	for (const vk::ShaderEXT& shader : shader_objects) vmc.logical_device.get().destroyShaderEXT(shader);
	shader_objects.clear();
	vmc.logical_device.get().destroyPipelineLayout(pipeline_layout);
}

void Pipeline::bind(vk::CommandBuffer cb) const
{
	cb.bindShadersEXT(stages, shader_objects);
	if (!graphics_state) return;
	const GraphicsState& state = *graphics_state;

	// fixed-function state that never varies across pipelines in this engine
	cb.setCullMode(vk::CullModeFlagBits::eNone);
	cb.setFrontFace(vk::FrontFace::eCounterClockwise);
	cb.setDepthClampEnableEXT(VK_FALSE);
	cb.setRasterizerDiscardEnable(VK_FALSE);
	cb.setDepthBiasEnable(VK_FALSE);
	cb.setPrimitiveRestartEnable(VK_FALSE);
	cb.setDepthTestEnable(VK_TRUE);
	cb.setDepthCompareOp(vk::CompareOp::eLess);
	cb.setDepthBoundsTestEnable(VK_FALSE);
	cb.setStencilTestEnable(VK_FALSE);
	cb.setAlphaToCoverageEnableEXT(VK_FALSE);
	const uint32_t sample_mask_word_count = (uint32_t(state.rasterization_samples) + 31) / 32;
	const std::array<uint32_t, 2> sample_mask{~0u, ~0u};
	cb.setSampleMaskEXT(state.rasterization_samples, vk::ArrayProxy<const uint32_t>(sample_mask_word_count, sample_mask.data()));

	// per-pipeline state
	cb.setVertexInputEXT(state.vertex_bindings, state.vertex_attributes);
	cb.setPrimitiveTopology(state.primitive_topology);
	cb.setPolygonModeEXT(state.polygon_mode);
	cb.setRasterizationSamplesEXT(state.rasterization_samples);
	cb.setDepthWriteEnable(state.depth_write_enable);
	cb.setColorBlendEnableEXT(0, state.color_blend_enable);
	cb.setColorBlendEquationEXT(0, state.color_blend_equation);
	cb.setColorWriteMaskEXT(0, state.color_write_mask);
}

const vk::PipelineLayout& Pipeline::get_layout() const
{
	return pipeline_layout;
}
} // namespace vkte
