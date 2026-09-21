#pragma once

#include <array>
#include <functional>
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

	void run_one_time_graphics(const std::function<void(vk::CommandBuffer&)>& record);
	void run_one_time_compute(const std::function<void(vk::CommandBuffer&)>& record);
	void run_one_time_transfer(const std::function<void(vk::CommandBuffer&)>& record);

	void submit_graphics(vk::ArrayProxy<const vk::SubmitInfo2> const& submit_infos) const;
	void submit_compute(vk::ArrayProxy<const vk::SubmitInfo2> const& submit_infos) const;
	void submit_transfer(vk::ArrayProxy<const vk::SubmitInfo2> const& submit_infos) const;

private:
	enum Type
	{
		GRAPHICS = 0,
		COMPUTE = 1,
		TRANSFER = 2,
		TYPE_COUNT
	};

	void run_one_time(const std::function<void(vk::CommandBuffer&)>& record, const vk::Queue& queue, Type type);

	const VulkanMainContext& vmc;
	std::vector<CommandPool> command_pools;
	std::vector<vk::CommandBuffer> one_time_cbs;
	std::vector<vk::CommandBuffer> command_buffers;
	std::array<vk::Semaphore, TYPE_COUNT> timeline_semaphores{};
	std::array<uint64_t, TYPE_COUNT> timeline_values{};
};
} // namespace vkte
