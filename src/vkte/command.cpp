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

	const vk::SemaphoreTypeCreateInfo stci(vk::SemaphoreType::eTimeline, 0);
	const vk::SemaphoreCreateInfo sci({}, &stci);
	for (uint32_t i = 0; i < TYPE_COUNT; i++)
	{
		timeline_semaphores[i] = vmc.logical_device.get().createSemaphore(sci);
		frame_waits[i] = vk::SemaphoreSubmitInfo(timeline_semaphores[i], 0, vk::PipelineStageFlagBits2::eAllCommands);
	}
}

void Command::destruct()
{
	for (auto& command_pool : command_pools) command_pool.destruct();
	command_pools.clear();
	for (const vk::Semaphore& semaphore : timeline_semaphores) vmc.logical_device.get().destroySemaphore(semaphore);
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

static void begin_cb(vk::CommandBuffer& cb)
{
	vk::CommandBufferBeginInfo cbbi;
	cbbi.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
	cb.begin(cbbi);
}

vk::CommandBuffer& Command::begin(CommandBufferHandle handle)
{
	vk::CommandBuffer& cb = get(handle);
	begin_cb(cb);
	return cb;
}

void Command::run_one_time_graphics(const std::function<void(vk::CommandBuffer&)>& record)
{
	run_one_time(record, vmc.get_graphics_queue(), GRAPHICS);
}

void Command::run_one_time_compute(const std::function<void(vk::CommandBuffer&)>& record)
{
	run_one_time(record, vmc.get_compute_queue(), COMPUTE);
}

void Command::run_one_time_transfer(const std::function<void(vk::CommandBuffer&)>& record)
{
	run_one_time(record, vmc.get_transfer_queue(), TRANSFER);
}

void Command::run_one_time(const std::function<void(vk::CommandBuffer&)>& record, const vk::Queue& queue, Type type)
{
	vk::CommandBuffer& cb = one_time_cbs[type];
	begin_cb(cb);
	record(cb);
	cb.end();
	const uint64_t value = ++timeline_values[type];
	const vk::CommandBufferSubmitInfo cbsi(cb);
	const vk::SemaphoreSubmitInfo signal_info(timeline_semaphores[type], value, vk::PipelineStageFlagBits2::eAllCommands);
	queue.submit2(vk::SubmitInfo2({}, {}, cbsi, signal_info));
	submitted_values[type] = value;
	const vk::SemaphoreWaitInfo swi({}, timeline_semaphores[type], value);
	VKTE_CHECK(vmc.logical_device.get().waitSemaphores(swi, uint64_t(-1)), "vkte: Failed to wait for timeline semaphore!");
	cb.reset();
}

void Command::begin_frame(uint32_t frame_index)
{
	const vk::SemaphoreWaitInfo swi({}, timeline_semaphores, frame_end_values[frame_index]);
	VKTE_CHECK(vmc.logical_device.get().waitSemaphores(swi, uint64_t(-1)), "vkte: Failed to wait for timeline semaphore!");
	const std::array<uint64_t, TYPE_COUNT>& previous_frame_end_values = frame_end_values[(frame_index + frames_in_flight - 1) % frames_in_flight];
	for (uint32_t i = 0; i < TYPE_COUNT; i++) frame_waits[i].value = previous_frame_end_values[i];
}

void Command::end_frame(uint32_t frame_index)
{
	frame_end_values[frame_index] = timeline_values;
}

void Command::wait_for_compute_submissions()
{
	frame_waits[COMPUTE].value = timeline_values[COMPUTE];
}

void Command::submit(Type type, const vk::Queue& queue, vk::ArrayProxy<const vk::SubmitInfo2> const& submit_infos)
{
	std::vector<vk::SubmitInfo2> infos(submit_infos.begin(), submit_infos.end());
	std::vector<std::vector<vk::SemaphoreSubmitInfo>> waits(infos.size());
	std::vector<std::vector<vk::SemaphoreSubmitInfo>> signals(infos.size());
	for (uint32_t i = 0; i < infos.size(); i++)
	{
		waits[i].assign(infos[i].pWaitSemaphoreInfos, infos[i].pWaitSemaphoreInfos + infos[i].waitSemaphoreInfoCount);
		waits[i].insert(waits[i].end(), frame_waits.begin(), frame_waits.end());
		signals[i].assign(infos[i].pSignalSemaphoreInfos, infos[i].pSignalSemaphoreInfos + infos[i].signalSemaphoreInfoCount);
		signals[i].emplace_back(timeline_semaphores[type], ++timeline_values[type], vk::PipelineStageFlagBits2::eAllCommands);
		infos[i].setWaitSemaphoreInfos(waits[i]);
		infos[i].setSignalSemaphoreInfos(signals[i]);
	}
	queue.submit2(infos);
	submitted_values[type] = timeline_values[type];
}

std::vector<Command::TimelineProgress> Command::get_timeline_progress() const
{
	std::vector<TimelineProgress> progress;
	for (uint32_t i = 0; i < TYPE_COUNT; i++) progress.push_back(TimelineProgress{.semaphore = timeline_semaphores[i], .submitted_value = submitted_values[i]});
	return progress;
}

void Command::submit_graphics(vk::ArrayProxy<const vk::SubmitInfo2> const& submit_infos)
{
	submit(GRAPHICS, vmc.get_graphics_queue(), submit_infos);
}

void Command::submit_compute(vk::ArrayProxy<const vk::SubmitInfo2> const& submit_infos)
{
	submit(COMPUTE, vmc.get_compute_queue(), submit_infos);
}

void Command::submit_transfer(vk::ArrayProxy<const vk::SubmitInfo2> const& submit_infos)
{
	submit(TRANSFER, vmc.get_transfer_queue(), submit_infos);
}
} // namespace vkte
