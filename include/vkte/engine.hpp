#pragma once

#include <memory>
#include <string>
#include <vector>
#include "vkte/component.hpp"
#include "vkte/device_timer.hpp"
#include "vkte/pipeline.hpp"
#include "vkte/resource_declarations.hpp"
#include "vkte/resource_handles.hpp"
#include "vkte/shader_repository.hpp"
#include "vkte/storage.hpp"
#include "vkte/thread_manager.hpp"
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
	void construct_components(const FrameSettings& settings);
#else
	void construct_components();
#endif
	// recompiles and rebuilds every declared pipeline; returns false if any compile failed, in which case no pipeline is rebuilt and every old one stays usable
	bool reload_shaders_all();
	void destruct_components();

	const vk::Pipeline& get_pipeline(PipelineHandle handle) const;
	const vk::PipelineLayout& get_pipeline_layout(PipelineHandle handle) const;
	const vk::DescriptorSetLayout& get_descriptor_set_layout(DescriptorSetLayoutHandle handle) const;
	const std::vector<vk::DescriptorSet>& get_descriptor_sets(DescriptorSetsHandle handle) const;
	void wait_idle() const;
	const vk::Device& get_device() const { return vmc.logical_device.get(); }
	uint32_t get_queue_family_index(QueueFamilyFlags queue) const;
#if ENABLE_VKTE_WINDOW
	Window& get_window() { return vmc.window; }
#endif
	VulkanMainContext& get_vmc() { return vmc; }
	VulkanCommandContext& get_vcc() { return vcc; }
	Storage& get_storage() { return storage; }
	ThreadManager& get_thread_manager() { return thread_manager; }

	SemaphoreHandle add_semaphore();
	void destroy(SemaphoreHandle handle);
	vk::Semaphore get(SemaphoreHandle handle) const;

	FenceHandle add_fence();
	void destroy(FenceHandle handle);
	vk::Fence get(FenceHandle handle) const;
	void wait_for_fence(FenceHandle handle) const;
	void reset_fence(FenceHandle handle) const;
	bool is_fence_finished(FenceHandle handle) const;

	// timer_count named timestamp slots backed by a single query pool
	DeviceTimerHandle add_device_timer(uint32_t timer_count);
	void destroy(DeviceTimerHandle handle);
	DeviceTimer& get(DeviceTimerHandle handle) const;

private:
	VulkanMainContext vmc;
	VulkanCommandContext vcc;
	Storage storage;
	ThreadManager thread_manager;
	ShaderRepository shader_repository;
	FrameSettings frame_settings;
	std::vector<Component*> components;
	ResourceDeclarations declarations;
	vk::DescriptorPool descriptor_pool;
	std::vector<vk::DescriptorSetLayout> descriptor_set_layouts;
	std::vector<std::vector<vk::DescriptorSet>> descriptor_sets;
	std::vector<vkte::Pipeline> pipelines;
	std::vector<vk::Semaphore> semaphores;
	std::vector<vk::Fence> fences;
	std::vector<std::unique_ptr<DeviceTimer>> device_timers;

	void build_descriptor_set_layouts();
	void build_pipelines();
	void build_descriptor_sets();
	void release_declared();
	void set_debug_name(vk::ObjectType type, uint64_t handle, const std::string& name) const;
	const char* owner_name(uint32_t component) const;
};
} // namespace vkte
