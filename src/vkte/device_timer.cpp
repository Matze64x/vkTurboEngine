#include "vkte/device_timer.hpp"

namespace vkte
{
DeviceTimer::DeviceTimer(const VulkanMainContext& vmc, uint32_t timer_count) : vmc(vmc)
{
	result_fetched.resize(timer_count, true);
	vk::QueryPoolCreateInfo qpci;
	qpci.queryType = vk::QueryType::eTimestamp;
	qpci.queryCount = timer_count * 2;
	qp = vmc.logical_device.get().createQueryPool(qpci);
	vk::PhysicalDeviceProperties pdp = vmc.physical_device.get().getProperties();
	timestamp_period = pdp.limits.timestampPeriod;
}

DeviceTimer::~DeviceTimer()
{
	vmc.logical_device.get().destroyQueryPool(qp);
}

void DeviceTimer::reset(vk::CommandBuffer& cb, uint32_t timer_index)
{
	cb.resetQueryPool(qp, timer_index * 2, 2);
}

void DeviceTimer::reset_all(vk::CommandBuffer& cb)
{
	cb.resetQueryPool(qp, 0, result_fetched.size());
}

void DeviceTimer::start(vk::CommandBuffer& cb, uint32_t timer_index, vk::PipelineStageFlagBits stage)
{
	cb.writeTimestamp(stage, qp, timer_index * 2);
}

void DeviceTimer::stop(vk::CommandBuffer& cb, uint32_t timer_index, vk::PipelineStageFlagBits stage)
{
	cb.writeTimestamp(stage, qp, timer_index * 2 + 1);
	result_fetched[timer_index] = false;
}
} // namespace vkte
