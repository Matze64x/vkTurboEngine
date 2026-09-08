#pragma once

#include "vulkan/vulkan.hpp"
#include "vkte/resource_handles.hpp"
#include "vkte/storage.hpp"
#include "vkte/vulkan_main_context.hpp"

namespace vkte
{
class Swapchain
{
public:
	Swapchain(const VulkanMainContext& vmc, VulkanCommandContext& vcc, Storage& storage);
	void construct(bool vsync);
	void destruct();
	void recreate(bool vsync);
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
	const VulkanMainContext& vmc;
	VulkanCommandContext& vcc;
	Storage& storage;
	vk::Extent2D extent;
	vk::SurfaceFormatKHR surface_format;
	vk::Format depth_format;
	vk::SwapchainKHR swapchain;
	ResourceHandle depth_buffer;
	std::vector<vk::Image> images;
	std::vector<vk::ImageView> image_views;

	vk::SwapchainKHR create_swapchain(bool vsync);
	void create_images();
	vk::PresentModeKHR choose_present_mode(bool vsync);
	vk::Extent2D choose_extent();
	vk::SurfaceFormatKHR choose_surface_format();
	vk::Format choose_depth_format();
};
} // namespace vkte
