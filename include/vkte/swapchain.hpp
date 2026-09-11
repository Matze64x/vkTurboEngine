#pragma once

#include "vulkan/vulkan.hpp"
#include "vkte/physical_device.hpp"
#include "vkte/queue_families.hpp"
#include "vkte/resource_handles.hpp"
#include "vkte/storage.hpp"

namespace vkte
{
class Engine;
class Window;

struct SwapchainSettings
{
	vk::SurfaceKHR surface;
	vk::SurfaceCapabilitiesKHR capabilities;
	vk::Extent2D extent;
	vk::SurfaceFormatKHR surface_format;
	vk::Format depth_format;
	vk::PresentModeKHR present_mode;
};

class Swapchain
{
public:
	const vk::SwapchainKHR& get() const;
	vk::Extent2D get_extent() const;
	vk::ImageView get_view(uint32_t idx) const;
	vk::Image get_image(uint32_t idx) const;
	vk::ImageView get_depth_view() const;
	vk::Image get_depth_image() const;
	vk::Format get_color_format() const;
	vk::Format get_depth_format() const;
	uint32_t get_image_count() const;

private:
	friend class Engine;
	static SwapchainSettings choose_settings(const PhysicalDevice& physical_device, const Window& window, vk::SurfaceKHR surface, bool vsync);
	void construct(const vk::Device& device, const QueueFamilies& queue_families, const SwapchainSettings& settings, Command& command, Storage& storage);
	void destruct(const vk::Device& device, Storage& storage);

	vk::Extent2D extent;
	vk::SurfaceFormatKHR surface_format;
	vk::Format depth_format;
	vk::SwapchainKHR swapchain;
	ResourceHandle depth_buffer;
	vk::Image depth_image;
	vk::ImageView depth_view;
	std::vector<vk::Image> images;
	std::vector<vk::ImageView> image_views;

	vk::SwapchainKHR create_swapchain(const vk::Device& device, const QueueFamilies& queue_families, const SwapchainSettings& settings);
	void create_images(const vk::Device& device);
};
} // namespace vkte
