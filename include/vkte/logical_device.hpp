#pragma once

#include <unordered_map>
#include "vulkan/vulkan.hpp"

#include "vkte/device_features.hpp"
#include "vkte/queue_families.hpp"
#include "vkte/physical_device.hpp"

namespace vkte
{
enum class QueueIndex
{
	Graphics,
	Compute,
	Transfer,
	Present
};

class LogicalDevice
{
public:

	LogicalDevice() = default;
	void construct(const PhysicalDevice& p_device, const DeviceFeatures& features, const QueueFamilies& queue_families, std::unordered_map<QueueIndex, vk::Queue>& queues);
	void destruct();
	const vk::Device& get() const;

private:
	vk::Device device;
};
} // namespace vkte
