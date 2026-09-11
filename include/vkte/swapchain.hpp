#pragma once

#include "vulkan/vulkan.hpp"
#include "vkte/resource_handles.hpp"
#include "vkte/storage.hpp"
#include "vkte/vulkan_main_context.hpp"

namespace vkte
{
class Engine;

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
	void construct(const VulkanMainContext& vmc, VulkanCommandContext& vcc, Storage& storage, bool vsync);
	void destruct(const VulkanMainContext& vmc, Storage& storage);

	vk::Extent2D extent;
	vk::SurfaceFormatKHR surface_format;
	vk::Format depth_format;
	vk::SwapchainKHR swapchain;
	ResourceHandle depth_buffer;
	vk::Image depth_image;
	vk::ImageView depth_view;
	std::vector<vk::Image> images;
	std::vector<vk::ImageView> image_views;

	vk::SwapchainKHR create_swapchain(const VulkanMainContext& vmc, bool vsync);
	void create_images(const VulkanMainContext& vmc);
	vk::PresentModeKHR choose_present_mode(const VulkanMainContext& vmc, bool vsync);
	vk::Extent2D choose_extent(const VulkanMainContext& vmc);
	vk::SurfaceFormatKHR choose_surface_format(const VulkanMainContext& vmc);
	vk::Format choose_depth_format(const VulkanMainContext& vmc);
};
} // namespace vkte
