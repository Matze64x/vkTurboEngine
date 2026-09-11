#include "vkte/command.hpp"

#include "vkte/vkte_log.hpp"

namespace vkte
{
Command::Command(const VulkanMainContext& vmc) : vmc(vmc), command_pools(TYPE_COUNT), one_time_cbs(TYPE_COUNT)
{}

void Command::construct()
{
	command_pools[GRAPHICS] = CommandPool(vmc.logical_device.get(), vmc.queue_families.get(QueueFamilyFlags::Graphics));
	command_pools[COMPUTE] = CommandPool(vmc.logical_device.get(), vmc.queue_families.get(QueueFamilyFlags::Compute));
	command_pools[TRANSFER] = CommandPool(vmc.logical_device.get(), vmc.queue_families.get(QueueFamilyFlags::Transfer));
	one_time_cbs[GRAPHICS] = command_pools[GRAPHICS].create_command_buffers(1)[0];
	one_time_cbs[COMPUTE] = command_pools[COMPUTE].create_command_buffers(1)[0];
	one_time_cbs[TRANSFER] = command_pools[TRANSFER].create_command_buffers(1)[0];
}

void Command::destruct()
{
	for (auto& command_pool : command_pools) command_pool.destruct();
	command_pools.clear();
}

CommandBufferHandle Command::add_command_buffer(QueueFamilyFlags queue)
{
	Type type = queue == QueueFamilyFlags::Graphics ? GRAPHICS : (queue == QueueFamilyFlags::Compute ? COMPUTE : TRANSFER);
	command_buffers.push_back(command_pools[type].create_command_buffers(1)[0]);
	return CommandBufferHandle{uint32_t(command_buffers.size() - 1)};
}

vk::CommandBuffer& Command::get(CommandBufferHandle handle)
{
	VKTE_ASSERT(handle.valid() && handle.id < command_buffers.size(), "vkte: Invalid command buffer handle!");
	return command_buffers.at(handle.id);
}

vk::CommandBuffer& Command::begin(CommandBufferHandle handle)
{
	return begin(get(handle));
}

vk::CommandBuffer& Command::get_one_time_graphics_buffer() { return begin(one_time_cbs[GRAPHICS]); }

vk::CommandBuffer& Command::get_one_time_compute_buffer() { return begin(one_time_cbs[COMPUTE]); }

vk::CommandBuffer& Command::get_one_time_transfer_buffer() { return begin(one_time_cbs[TRANSFER]); }

vk::CommandBuffer& Command::begin(vk::CommandBuffer& cb)
{
	vk::CommandBufferBeginInfo cbbi;
	cbbi.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
	cb.begin(cbbi);
	return cb;
}

void Command::submit_graphics(const vk::CommandBuffer& cb, bool wait_idle) const
{
	submit(cb, vmc.get_graphics_queue(), wait_idle);
}

void Command::submit_compute(const vk::CommandBuffer& cb, bool wait_idle) const
{
	submit(cb, vmc.get_compute_queue(), wait_idle);
}

void Command::submit_transfer(const vk::CommandBuffer& cb, bool wait_idle) const
{
	submit(cb, vmc.get_transfer_queue(), wait_idle);
}

void Command::submit_graphics(vk::ArrayProxy<const vk::SubmitInfo> const& submit_infos, vk::Fence fence) const
{
	vmc.get_graphics_queue().submit(submit_infos, fence);
}

void Command::submit_compute(vk::ArrayProxy<const vk::SubmitInfo> const& submit_infos, vk::Fence fence) const
{
	vmc.get_compute_queue().submit(submit_infos, fence);
}

void Command::submit_transfer(vk::ArrayProxy<const vk::SubmitInfo> const& submit_infos, vk::Fence fence) const
{
	vmc.get_transfer_queue().submit(submit_infos, fence);
}

void Command::submit(const vk::CommandBuffer& cb, const vk::Queue& queue, bool wait_idle) const
{
	cb.end();
	vk::SubmitInfo submit_info;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &cb;
	queue.submit(submit_info);
	if (wait_idle) queue.waitIdle();
	cb.reset();
}
} // namespace vkte
