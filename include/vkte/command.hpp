#pragma once

#include "vulkan/vulkan.hpp"
#include "vkte/command_pool.hpp"
#include "vkte/resource_handles.hpp"
#include "vkte/vulkan_main_context.hpp"

namespace vkte
{
class Command
{
public:
	Command(const VulkanMainContext& vmc);
	void construct();
	void destruct();

	CommandBufferHandle add_command_buffer(QueueFamilyFlags queue);
	vk::CommandBuffer& get(CommandBufferHandle handle);
	vk::CommandBuffer& begin(CommandBufferHandle handle);

	vk::CommandBuffer& get_one_time_graphics_buffer();
	vk::CommandBuffer& get_one_time_compute_buffer();
	vk::CommandBuffer& get_one_time_transfer_buffer();

	void submit_graphics(const vk::CommandBuffer& cb, bool wait_idle) const;
	void submit_compute(const vk::CommandBuffer& cb, bool wait_idle) const;
	void submit_transfer(const vk::CommandBuffer& cb, bool wait_idle) const;
	void submit_graphics(vk::ArrayProxy<const vk::SubmitInfo> const& submit_infos, vk::Fence fence = {}) const;
	void submit_compute(vk::ArrayProxy<const vk::SubmitInfo> const& submit_infos, vk::Fence fence = {}) const;
	void submit_transfer(vk::ArrayProxy<const vk::SubmitInfo> const& submit_infos, vk::Fence fence = {}) const;

private:
	enum Type
	{
		GRAPHICS = 0,
		COMPUTE = 1,
		TRANSFER = 2,
		TYPE_COUNT
	};

	vk::CommandBuffer& begin(vk::CommandBuffer& cb);
	void submit(const vk::CommandBuffer& cb, const vk::Queue& queue, bool wait_idle) const;

	const VulkanMainContext& vmc;
	std::vector<CommandPool> command_pools;
	std::vector<vk::CommandBuffer> one_time_cbs;
	std::vector<vk::CommandBuffer> command_buffers;
};
} // namespace vkte
