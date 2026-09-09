#pragma once

#include <string>
#include <vector>
#include "vkte/component.hpp"
#include "vkte/pipeline.hpp"
#include "vkte/resource_declarations.hpp"
#include "vkte/resource_handles.hpp"
#include "vkte/shader_repository.hpp"
#include "vkte/storage.hpp"
#include "vkte/vulkan_command_context.hpp"
#include "vkte/vulkan_main_context.hpp"

namespace vkte
{
struct EngineSettings
{
	Features features;
	std::string shader_root_dir;
#if ENABLE_VKTE_WINDOW
	std::string window_title = "vkte";
	uint32_t window_width = 1920;
	uint32_t window_height = 1080;
#endif
};

struct FrameSettings
{
	vk::Format color_format = vk::Format::eUndefined;
	vk::Format depth_format = vk::Format::eUndefined;
	vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
	uint32_t frames_in_flight = 1;
};

class Engine
{
public:
	explicit Engine(const EngineSettings& settings);
	~Engine();

	// the component must outlive the Engine
	void register_component(Component& component);
	// collects every component's declarations and builds all of them
#if ENABLE_VKTE_WINDOW
	void construct_all(const FrameSettings& settings);
#else
	void construct_all();
#endif
	// recompiles and rebuilds every declared pipeline; returns false if any compile failed, in which case no pipeline is rebuilt and every old one stays usable
	bool reload_shaders_all();
	void destruct_all();

	const Pipeline& get_pipeline(PipelineHandle handle) const;
	const vk::DescriptorSetLayout& get_descriptor_set_layout(DescriptorSetLayoutHandle handle) const;
	const std::vector<vk::DescriptorSet>& get_descriptor_sets(DescriptorSetsHandle handle) const;
	VulkanMainContext& get_vmc() { return vmc; }
	VulkanCommandContext& get_vcc() { return vcc; }
	Storage& get_storage() { return storage; }

private:
	VulkanMainContext vmc;
	VulkanCommandContext vcc;
	Storage storage;
	ShaderRepository shader_repository;
	FrameSettings frame_settings;
	std::vector<Component*> components;
	ResourceDeclarations declarations;
	vk::DescriptorPool descriptor_pool;
	std::vector<vk::DescriptorSetLayout> descriptor_set_layouts;
	std::vector<std::vector<vk::DescriptorSet>> descriptor_sets;
	std::vector<vkte::Pipeline> pipelines;

	void build_descriptor_set_layouts();
	void build_pipelines();
	void build_descriptor_sets();
	void release_declared();
	void set_debug_name(vk::ObjectType type, uint64_t handle, const std::string& name) const;
	const char* owner_name(uint32_t component) const;
};
} // namespace vkte
