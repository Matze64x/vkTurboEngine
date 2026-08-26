#include "vkte/pipeline.hpp"

#include <fstream>
#include <filesystem>
#include <iostream>
#include "vkte/image.hpp"
#include "vkte/vkte_log.hpp"
#include "vkte/vkte_log.hpp"

namespace vkte
{
Pipeline::Pipeline(const VulkanMainContext& vmc, Pipeline::Type type) : vmc(vmc), type(type)
{
	if (type == Type::Graphics) graphics_settings = std::make_unique<GraphicsSettings>();
	else if (type == Type::Compute) compute_settings = std::make_unique<ComputeSettings>();
}

Pipeline::GraphicsSettings& Pipeline::get_graphics_settings()
{
	VKTE_ASSERT(type == Type::Graphics, "vkte: Invalid access to graphics pipeline settings!");
	return *graphics_settings;
}

Pipeline::ComputeSettings& Pipeline::get_compute_settings()
{
	VKTE_ASSERT(type == Type::Compute, "vkte: Invalid access to compute pipeline settings!");
	return *compute_settings;
}

std::string get_slang_stage(vk::ShaderStageFlagBits stage_flag)
{
	if (stage_flag & vk::ShaderStageFlagBits::eVertex) return "vertex";
	if (stage_flag & vk::ShaderStageFlagBits::eTessellationControl) return "tesscontrol";
	if (stage_flag & vk::ShaderStageFlagBits::eTessellationEvaluation) return "tesseval";
	if (stage_flag & vk::ShaderStageFlagBits::eGeometry) return "geometry";
	if (stage_flag & vk::ShaderStageFlagBits::eFragment) return "fragment";
	if (stage_flag & vk::ShaderStageFlagBits::eCompute) return "compute";
	// Ray tracing stages
	if (stage_flag & vk::ShaderStageFlagBits::eRaygenKHR) return "raygeneration";
	if (stage_flag & vk::ShaderStageFlagBits::eIntersectionKHR) return "intersection";
	if (stage_flag & vk::ShaderStageFlagBits::eAnyHitKHR) return "anyhit";
	if (stage_flag & vk::ShaderStageFlagBits::eClosestHitKHR) return "closesthit";
	if (stage_flag & vk::ShaderStageFlagBits::eMissKHR) return "miss";
	if (stage_flag & vk::ShaderStageFlagBits::eCallableKHR) return "callable";
	// Mesh shading stages (EXT/NV aliases)
#ifdef VK_EXT_mesh_shader
	if (stage_flag & vk::ShaderStageFlagBits::eTaskEXT) return "task";
	if (stage_flag & vk::ShaderStageFlagBits::eMeshEXT) return "mesh";
#endif
#ifdef VK_NV_mesh_shader
	if (stage_flag & vk::ShaderStageFlagBits::eTaskNV) return "task";
	if (stage_flag & vk::ShaderStageFlagBits::eMeshNV) return "mesh";
#endif
	VKTE_ASSERT(false, "vkte: Unsupported slang shader stage flag");
	return "";
}

bool is_command_available(const std::string& command)
{
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
	std::string check = "where " + command + " >nul 2>nul";
#else
	std::string check = "command -v " + command + " >/dev/null 2>&1";
#endif
	return std::system(check.c_str()) == 0;
}

bool compile_shader(const vk::Device& device, const std::string& shader_root_dir, const Shader& shader, vk::PipelineShaderStageCreateInfo& pssci, vk::SpecializationInfo& spec_info)
{
	std::filesystem::path shader_dir(shader_root_dir);
	std::filesystem::path shader_bin_dir(shader_dir / "bin/");
	if (!std::filesystem::exists(shader_bin_dir)) std::filesystem::create_directory(shader_bin_dir);
	std::filesystem::path shader_file(shader_dir / shader.name);
	std::filesystem::path shader_bin_file(shader_bin_dir / (shader.name + ".spv"));
	std::string command;
	std::string args;
	if (shader.lang == Language::Glsl)
	{
		command = "glslc";
		args = std::format("--target-env=vulkan1.4 -O -o {0} {1}", shader_bin_file.string(), shader_file.string());
	}
	else if (shader.lang == Language::Slang)
	{
		command = "slangc";
		std::string stage = get_slang_stage(shader.stage_flag);
		// enable all capabilities to prevent any warnings about implicit upgrades
		args = std::format("-target spirv -emit-spirv-directly -profile spirv_1_5+all -fvk-use-scalar-layout -matrix-layout-column-major -entry main -stage {2} -o {0} {1}", shader_bin_file.string(), shader_file.string(), stage);
	}
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
	command += ".exe";
#elif __linux__
#endif
	if (is_command_available(command))
	{
		VKTE_ASSERT(std::filesystem::exists(shader_file), "vkte: Failed to find shader file \"" + shader.name + "\"");
		if (std::filesystem::exists(shader_bin_file)) std::filesystem::remove(shader_bin_file);
		system((command + " " + args).c_str());
		if (!std::filesystem::exists(shader_bin_file)) return false;
	}
	else
	{
		if (!std::filesystem::exists(shader_bin_file))
		{
			VKTE_ERROR("vkte: Compiler \"{}\" not available and no cached SPIR-V found for shader \"{}\"", command, shader.name);
			return false;
		}
		VKTE_WARN("vkte: Compiler \"{}\" not available, using cached SPIR-V for shader \"{}\"", command, shader.name);
	}
	std::ifstream file(shader_bin_file.string(), std::ios::binary);
	VKTE_ASSERT(file.is_open(), "vkte: Failed to open shader file \"" + shader.name + "\"");
	std::ostringstream file_stream;
	file_stream << file.rdbuf();
	std::string source = file_stream.str();

	vk::ShaderModuleCreateInfo smci;
	smci.codeSize = source.size();
	smci.pCode = reinterpret_cast<const uint32_t*>(source.c_str());
	pssci.module = device.createShaderModule(smci);
	pssci.stage = shader.stage_flag;
	pssci.pName = "main";

	spec_info = vk::SpecializationInfo(shader.get_spec_entries().size(), shader.get_spec_entries().data(), sizeof(uint32_t) * shader.get_spec_entries_data().size(), shader.get_spec_entries_data().data());
	pssci.pSpecializationInfo = &spec_info;
	return true;
}

bool Pipeline::compile_shaders()
{
	for (vk::PipelineShaderStageCreateInfo& pssci : shader_stages) vmc.logical_device.get().destroyShaderModule(pssci.module);
	shader_stages.clear();
	spec_infos.clear();
	if (type == Type::Graphics)
	{
		shader_stages.resize(graphics_settings->shaders.size());
		spec_infos.resize(graphics_settings->shaders.size());
		for (int32_t i = 0; i < graphics_settings->shaders.size(); i++)
		{
			if (!compile_shader(vmc.logical_device.get(), vmc.shader_root_dir, graphics_settings->shaders[i], shader_stages[i], spec_infos[i])) return false;
		}
	}
	else if (type == Type::Compute)
	{
		shader_stages.resize(1);
		spec_infos.resize(1);
		if (!compile_shader(vmc.logical_device.get(), vmc.shader_root_dir, compute_settings->shader, shader_stages[0], spec_infos[0])) return false;
	}
	return true;
}

void Pipeline::construct()
{
	if (type == Type::Graphics)
	{
		std::vector<vk::DynamicState> dynamic_states = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
		if (vmc.get_features().device_features.dynamic_polygon_mode) dynamic_states.push_back(vk::DynamicState::ePolygonModeEXT);
		if (vmc.get_features().device_features.dynamic_line_width) dynamic_states.push_back(vk::DynamicState::eLineWidth);
		vk::PipelineDynamicStateCreateInfo pdsci;
		pdsci.dynamicStateCount = dynamic_states.size();
		pdsci.pDynamicStates = dynamic_states.data();

		vk::PipelineVertexInputStateCreateInfo pvisci;
		pvisci.vertexBindingDescriptionCount = graphics_settings->binding_descriptions.size();
		pvisci.pVertexBindingDescriptions = graphics_settings->binding_descriptions.data();
		pvisci.vertexAttributeDescriptionCount = graphics_settings->attribute_description.size();
		pvisci.pVertexAttributeDescriptions = graphics_settings->attribute_description.data();

		vk::PipelineInputAssemblyStateCreateInfo piasci;
		piasci.topology = graphics_settings->primitive_topology;
		piasci.primitiveRestartEnable = VK_FALSE;

		vk::PipelineViewportStateCreateInfo pvsci;
		pvsci.viewportCount = 1;
		pvsci.scissorCount = 1;

		vk::PipelineRasterizationStateCreateInfo prsci;
		prsci.depthClampEnable = VK_FALSE;
		prsci.rasterizerDiscardEnable = VK_FALSE;
		prsci.polygonMode = graphics_settings->polygon_mode;
		prsci.lineWidth = 1.0f;
		prsci.cullMode = vk::CullModeFlagBits::eNone;
		prsci.frontFace = vk::FrontFace::eCounterClockwise;
		prsci.depthBiasEnable = VK_FALSE;
		prsci.depthBiasConstantFactor = 0.0f;
		prsci.depthBiasClamp = 0.0f;
		prsci.depthBiasSlopeFactor = 0.0f;

		vk::PipelineMultisampleStateCreateInfo pmssci;
		pmssci.sampleShadingEnable = VK_TRUE;
		pmssci.rasterizationSamples = graphics_settings->rasterization_samples;
		pmssci.minSampleShading = 0.4f;
		pmssci.pSampleMask = nullptr;
		pmssci.alphaToCoverageEnable = VK_FALSE;
		pmssci.alphaToOneEnable = VK_FALSE;

		std::vector<vk::PipelineColorBlendAttachmentState> pcbas(graphics_settings->color_formats.size());
		for (uint32_t i = 0; i < graphics_settings->color_formats.size(); i++)
		{
			pcbas[i].colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
			if (graphics_settings->additive_blending)
			{
				pcbas[i].blendEnable = VK_TRUE;
				pcbas[i].srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
				pcbas[i].dstColorBlendFactor = vk::BlendFactor::eOne;
			}
			else
		{
				pcbas[i].blendEnable = VK_FALSE;
				pcbas[i].srcColorBlendFactor = vk::BlendFactor::eOne;
				pcbas[i].dstColorBlendFactor = vk::BlendFactor::eZero;
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
		plci.pSetLayouts = graphics_settings->set_layout;
		plci.pushConstantRangeCount = graphics_settings->pcrs.size();
		plci.pPushConstantRanges = graphics_settings->pcrs.data();

		pipeline_layout = vmc.logical_device.get().createPipelineLayout(plci);

		vk::PipelineDepthStencilStateCreateInfo pdssci;
		pdssci.depthTestEnable = VK_TRUE;
		if (graphics_settings->additive_blending) pdssci.depthWriteEnable = VK_FALSE;
		else pdssci.depthWriteEnable = VK_TRUE;
		pdssci.depthCompareOp = vk::CompareOp::eLess;
		pdssci.depthBoundsTestEnable = VK_FALSE;
		pdssci.minDepthBounds = 0.0f;
		pdssci.maxDepthBounds = 1.0f;
		pdssci.stencilTestEnable = VK_FALSE;
		pdssci.front = vk::StencilOpState{};
		pdssci.back = vk::StencilOpState{};

		vk::PipelineRenderingCreateInfo prci;
		prci.colorAttachmentCount = graphics_settings->color_formats.size();
		prci.pColorAttachmentFormats = graphics_settings->color_formats.data();
		prci.depthAttachmentFormat = graphics_settings->depth_format;
		prci.stencilAttachmentFormat = has_stencil(graphics_settings->depth_format) ? graphics_settings->depth_format : vk::Format::eUndefined;

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
	else if (type == Type::Compute)
	{
		vk::PushConstantRange pcr;
		pcr.offset = 0;
		pcr.size = compute_settings->push_constant_byte_size;
		pcr.stageFlags = vk::ShaderStageFlagBits::eCompute;

		vk::PipelineLayoutCreateInfo plci;
		plci.setLayoutCount = 1;
		plci.pSetLayouts = compute_settings->set_layout;
		if (compute_settings->push_constant_byte_size > 0)
		{
			plci.pushConstantRangeCount = 1;
			plci.pPushConstantRanges = &pcr;
		}

		pipeline_layout = vmc.logical_device.get().createPipelineLayout(plci);

		vk::ComputePipelineCreateInfo cpci;
		cpci.stage = shader_stages[0];
		cpci.layout = pipeline_layout;

		vk::ResultValue<vk::Pipeline> comute_pipeline_result_value = vmc.logical_device.get().createComputePipeline(VK_NULL_HANDLE, cpci);
		VKTE_CHECK(comute_pipeline_result_value.result, "Failed to create compute pipeline!");
		pipeline = comute_pipeline_result_value.value;
	}
}

void Pipeline::reconstruct()
{
	vmc.logical_device.get().destroyPipeline(pipeline);
	vmc.logical_device.get().destroyPipelineLayout(pipeline_layout);
	construct();
}

void Pipeline::destruct()
{
	for (vk::PipelineShaderStageCreateInfo& pssci : shader_stages) vmc.logical_device.get().destroyShaderModule(pssci.module);
	shader_stages.clear();
	spec_infos.clear();
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
