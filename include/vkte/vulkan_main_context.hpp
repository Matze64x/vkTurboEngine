#pragma once

#include "vulkan/vulkan.hpp"
#include "vkte/queue_families.hpp"
#include "vkte/logical_device.hpp"
#include "vkte/physical_device.hpp"
#if ENABLE_VKTE_WINDOW
#include "vkte_window/window.hpp"
#endif
#include "vk_mem_alloc.h"

namespace vkte
{
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
#if ENABLE_VKTE_WINDOW
	void construct(const std::string& title, const uint32_t width, const uint32_t height, const Features& features);
#else
	void construct(const Features& features);
#endif
	void destruct();
#if ENABLE_VKTE_WINDOW
	std::vector<vk::SurfaceFormatKHR> get_surface_formats() const;
	std::vector<vk::PresentModeKHR> get_surface_present_modes() const;
	vk::SurfaceCapabilitiesKHR get_surface_capabilities() const;
#endif
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
#if ENABLE_VKTE_WINDOW
	Window window;
#endif
	Instance instance;
	vk::DebugUtilsMessengerEXT debug_messenger;
	vk::SurfaceKHR surface;
	PhysicalDevice physical_device;
	QueueFamilies queue_families;
	LogicalDevice logical_device;
	VmaAllocator va;
};
} // namespace vkte
