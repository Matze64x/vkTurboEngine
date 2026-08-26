#pragma once

#include <unordered_map>
#include "vulkan/vulkan.hpp"

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
	struct Features
	{
		bool dynamic_polygon_mode = false;
		bool dynamic_line_width = false;
		bool ray_query = false;
		bool acceleration_structure = false;
		bool shader_atomic_float = false;
	};

	LogicalDevice() = default;
	void construct(const PhysicalDevice& p_device, const Features& features, const QueueFamilies& queue_families, std::unordered_map<QueueIndex, vk::Queue>& queues);
	void destruct();
	const vk::Device& get() const;

private:
	vk::Device device;
};
} // namespace vkte
