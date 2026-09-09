#include "vkte/pipeline.hpp"

#include "vkte/image.hpp"
#include "vkte/shader_repository.hpp"
#include "vkte/vkte_log.hpp"
#include "vkte/vkte_log.hpp"

namespace vkte
{
Pipeline::Pipeline(const VulkanMainContext& vmc) : vmc(vmc)
{}

void Pipeline::construct(const GraphicsSettings& settings, const ShaderRepository& shader_repository, vk::DescriptorSetLayout* set_layout)
{
	std::vector<vk::PipelineShaderStageCreateInfo> shader_stages(settings.shaders.size());
	for (size_t i = 0; i < settings.shaders.size(); i++) shader_stages[i] = shader_repository.get_shader_stage(settings.shaders[i]);

	std::vector<vk::DynamicState> dynamic_states = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
	if (vmc.get_features().device_features.dynamic_polygon_mode) dynamic_states.push_back(vk::DynamicState::ePolygonModeEXT);
	if (vmc.get_features().device_features.dynamic_line_width) dynamic_states.push_back(vk::DynamicState::eLineWidth);
	vk::PipelineDynamicStateCreateInfo pdsci;
	pdsci.dynamicStateCount = dynamic_states.size();
	pdsci.pDynamicStates = dynamic_states.data();

	vk::PipelineVertexInputStateCreateInfo pvisci;
	pvisci.vertexBindingDescriptionCount = settings.binding_descriptions.size();
	pvisci.pVertexBindingDescriptions = settings.binding_descriptions.data();
	pvisci.vertexAttributeDescriptionCount = settings.attribute_description.size();
	pvisci.pVertexAttributeDescriptions = settings.attribute_description.data();

	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = settings.primitive_topology;
	piasci.primitiveRestartEnable = VK_FALSE;

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.scissorCount = 1;

	vk::PipelineRasterizationStateCreateInfo prsci;
	prsci.depthClampEnable = VK_FALSE;
	prsci.rasterizerDiscardEnable = VK_FALSE;
	prsci.polygonMode = settings.polygon_mode;
	prsci.lineWidth = 1.0f;
	prsci.cullMode = vk::CullModeFlagBits::eNone;
	prsci.frontFace = vk::FrontFace::eCounterClockwise;
	prsci.depthBiasEnable = VK_FALSE;
	prsci.depthBiasConstantFactor = 0.0f;
	prsci.depthBiasClamp = 0.0f;
	prsci.depthBiasSlopeFactor = 0.0f;

	vk::PipelineMultisampleStateCreateInfo pmssci;
	pmssci.sampleShadingEnable = VK_TRUE;
	pmssci.rasterizationSamples = settings.rasterization_samples;
	pmssci.minSampleShading = 0.4f;
	pmssci.pSampleMask = nullptr;
	pmssci.alphaToCoverageEnable = VK_FALSE;
	pmssci.alphaToOneEnable = VK_FALSE;

	std::vector<vk::PipelineColorBlendAttachmentState> pcbas(settings.color_formats.size());
	for (uint32_t i = 0; i < settings.color_formats.size(); i++)
	{
		pcbas[i].colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		switch (settings.blend_mode)
		{
		case BlendMode::Additive:
			pcbas[i].blendEnable = VK_TRUE;
			pcbas[i].srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
			pcbas[i].dstColorBlendFactor = vk::BlendFactor::eOne;
			break;
		case BlendMode::AlphaBlend:
			pcbas[i].blendEnable = VK_TRUE;
			pcbas[i].srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
			pcbas[i].dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
			break;
		case BlendMode::None:
		default:
			pcbas[i].blendEnable = VK_FALSE;
			pcbas[i].srcColorBlendFactor = vk::BlendFactor::eOne;
			pcbas[i].dstColorBlendFactor = vk::BlendFactor::eZero;
			break;
		}
		pcbas[i].colorBlendOp = vk::BlendOp::eAdd;
		pcbas[i].srcAlphaBlendFactor = vk::BlendFactor::eOne;
		pcbas[i].dstAlphaBlendFactor = vk::BlendFactor::eZero;
		pcbas[i].alphaBlendOp = vk::BlendOp::eAdd;
	}

	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = pcbas.size();
	pcbsci.pAttachments = pcbas.data();
	pcbsci.blendConstants[0] = 0.0f;
	pcbsci.blendConstants[1] = 0.0f;
	pcbsci.blendConstants[2] = 0.0f;
	pcbsci.blendConstants[3] = 0.0f;

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = 1;
	plci.pSetLayouts = set_layout;
	plci.pushConstantRangeCount = settings.pcrs.size();
	plci.pPushConstantRanges = settings.pcrs.data();

	pipeline_layout = vmc.logical_device.get().createPipelineLayout(plci);

	vk::PipelineDepthStencilStateCreateInfo pdssci;
	pdssci.depthTestEnable = VK_TRUE;
	if (settings.blend_mode == BlendMode::None) pdssci.depthWriteEnable = VK_TRUE;
	else pdssci.depthWriteEnable = VK_FALSE;
	pdssci.depthCompareOp = vk::CompareOp::eLess;
	pdssci.depthBoundsTestEnable = VK_FALSE;
	pdssci.minDepthBounds = 0.0f;
	pdssci.maxDepthBounds = 1.0f;
	pdssci.stencilTestEnable = VK_FALSE;
	pdssci.front = vk::StencilOpState{};
	pdssci.back = vk::StencilOpState{};

	vk::PipelineRenderingCreateInfo prci;
	prci.colorAttachmentCount = settings.color_formats.size();
	prci.pColorAttachmentFormats = settings.color_formats.data();
	prci.depthAttachmentFormat = settings.depth_format;
	prci.stencilAttachmentFormat = has_stencil(settings.depth_format) ? settings.depth_format : vk::Format::eUndefined;

	vk::GraphicsPipelineCreateInfo gpci;
	gpci.pNext = &prci;
	gpci.stageCount = shader_stages.size();
	gpci.pStages = shader_stages.data();
	gpci.pVertexInputState = &pvisci;
	gpci.pInputAssemblyState = &piasci;
	gpci.pViewportState = &pvsci;
	gpci.pRasterizationState = &prsci;
	gpci.pMultisampleState = &pmssci;
	gpci.pDepthStencilState = &pdssci;
	gpci.pColorBlendState = &pcbsci;
	gpci.pDynamicState = &pdsci;
	gpci.layout = pipeline_layout;
	gpci.basePipelineHandle = VK_NULL_HANDLE;
	gpci.basePipelineIndex = -1;

	vk::ResultValue<vk::Pipeline> pipeline_result_value = vmc.logical_device.get().createGraphicsPipeline(VK_NULL_HANDLE, gpci);
	VKTE_CHECK(pipeline_result_value.result, "Failed to create pipeline!");
	pipeline = pipeline_result_value.value;
}

void Pipeline::construct(const ComputeSettings& settings, const ShaderRepository& shader_repository, vk::DescriptorSetLayout* set_layout)
{
	vk::PipelineShaderStageCreateInfo shader_stage = shader_repository.get_shader_stage(settings.shader);

	vk::PushConstantRange pcr;
	pcr.offset = 0;
	pcr.size = settings.push_constant_byte_size;
	pcr.stageFlags = vk::ShaderStageFlagBits::eCompute;

	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = 1;
	plci.pSetLayouts = set_layout;
	if (settings.push_constant_byte_size > 0)
	{
		plci.pushConstantRangeCount = 1;
		plci.pPushConstantRanges = &pcr;
	}

	pipeline_layout = vmc.logical_device.get().createPipelineLayout(plci);

	vk::ComputePipelineCreateInfo cpci;
	cpci.stage = shader_stage;
	cpci.layout = pipeline_layout;

	vk::ResultValue<vk::Pipeline> compute_pipeline_result_value = vmc.logical_device.get().createComputePipeline(VK_NULL_HANDLE, cpci);
	VKTE_CHECK(compute_pipeline_result_value.result, "Failed to create compute pipeline!");
	pipeline = compute_pipeline_result_value.value;
}

void Pipeline::destruct()
{
	vmc.logical_device.get().destroyPipeline(pipeline);
	vmc.logical_device.get().destroyPipelineLayout(pipeline_layout);
}

const vk::Pipeline& Pipeline::get() const
{
	return pipeline;
}

const vk::PipelineLayout& Pipeline::get_layout() const
{
	return pipeline_layout;
}
} // namespace vkte
