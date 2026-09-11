#pragma once

#include <memory>
#include "vulkan/vulkan.hpp"
#include "vkte/queue_families.hpp"
#include "vkte/logical_device.hpp"
#include "vkte/physical_device.hpp"
#include "vk_mem_alloc.h"

namespace vkte
{
class Window;

struct Features
{
	bool khronos_validation = false;
	bool swapchain = false;
	LogicalDevice::Features device_features;
};

class VulkanMainContext
{
public:
	VulkanMainContext() = default;
	void construct(const Features& features, std::unique_ptr<Window>& window);
	void destruct();
	const vk::Queue& get_graphics_queue() const;
	const vk::Queue& get_transfer_queue() const;
	const vk::Queue& get_compute_queue() const;
	const vk::Queue& get_present_queue() const;
	const Features& get_features() const;

private:
	std::unordered_map<QueueIndex, vk::Queue> queues;
	Features features;

	void create_vma_allocator();
	void setup_debug_messenger();

public:
	vk::detail::DynamicLoader dl;
	Instance instance;
	vk::DebugUtilsMessengerEXT debug_messenger;
	vk::SurfaceKHR surface;
	PhysicalDevice physical_device;
	QueueFamilies queue_families;
	LogicalDevice logical_device;
	VmaAllocator va;
};
} // namespace vkte
