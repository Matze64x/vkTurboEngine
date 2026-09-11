#include "vkte/swapchain.hpp"

#include "SDL3/SDL_video.h"
#include "vkte/vkte_log.hpp"

namespace vkte
{
const vk::SwapchainKHR& Swapchain::get() const
{
	return swapchain;
}

vk::Extent2D Swapchain::get_extent() const
{
	return extent;
}

vk::ImageView Swapchain::get_view(uint32_t idx) const
{
	return image_views[idx];
}

vk::Image Swapchain::get_image(uint32_t idx) const
{
	return images[idx];
}

vk::ImageView Swapchain::get_depth_view() const
{
	return depth_view;
}

vk::Image Swapchain::get_depth_image() const
{
	return depth_image;
}

vk::Format Swapchain::get_color_format() const
{
	return surface_format.format;
}

vk::Format Swapchain::get_depth_format() const
{
	return depth_format;
}

uint32_t Swapchain::get_image_count() const
{
	return images.size();
}

void Swapchain::construct(const VulkanMainContext& vmc, VulkanCommandContext& vcc, Storage& storage, bool vsync)
{
	extent = choose_extent(vmc);
	surface_format = choose_surface_format(vmc);
	depth_format = choose_depth_format(vmc);
	swapchain = create_swapchain(vmc, vsync);
	depth_buffer = storage.add_image("depth_buffer", extent.width, extent.height, vk::ImageUsageFlagBits::eDepthStencilAttachment, depth_format, vk::SampleCountFlagBits::e1, false, 0, QueueFamilyFlags::Graphics);
	Image& depth_buffer_image = storage.get_image(depth_buffer);
	depth_buffer_image.transition_image_layout(vcc, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eNone, vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
	depth_image = depth_buffer_image.get_image();
	depth_view = depth_buffer_image.get_view();
	create_images(vmc);
}

void Swapchain::destruct(const VulkanMainContext& vmc, Storage& storage)
{
	for (auto& image_view : image_views) vmc.logical_device.get().destroyImageView(image_view);
	image_views.clear();
	storage.destroy(depth_buffer);
	vmc.logical_device.get().destroySwapchainKHR(swapchain);
}

vk::SwapchainKHR Swapchain::create_swapchain(const VulkanMainContext& vmc, bool vsync)
{
	vk::SurfaceCapabilitiesKHR capabilities = vmc.get_surface_capabilities();
	uint32_t image_count = capabilities.maxImageCount > 0 ? std::min(capabilities.minImageCount + 1, capabilities.maxImageCount) : capabilities.minImageCount + 1;

	vk::SwapchainCreateInfoKHR sci;
	sci.surface = vmc.surface;
	sci.minImageCount = image_count;
	sci.imageFormat = surface_format.format;
	sci.imageColorSpace = surface_format.colorSpace;
	sci.imageExtent = extent;
	sci.imageArrayLayers = 1;
	sci.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
	sci.preTransform = capabilities.currentTransform;
	sci.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	sci.presentMode = choose_present_mode(vmc, vsync);
	sci.clipped = VK_TRUE;
	sci.oldSwapchain = VK_NULL_HANDLE;
	std::vector<uint32_t> queue_family_indices = vmc.queue_families.get(QueueFamilyFlags::Graphics | QueueFamilyFlags::Present);
	if (queue_family_indices.size() > 1)
	{
		sci.imageSharingMode = vk::SharingMode::eConcurrent;
		sci.queueFamilyIndexCount = queue_family_indices.size();
		sci.pQueueFamilyIndices = queue_family_indices.data();
	}
	else
	{
		sci.imageSharingMode = vk::SharingMode::eExclusive;
	}
	return vmc.logical_device.get().createSwapchainKHR(sci);
}

void Swapchain::create_images(const VulkanMainContext& vmc)
{
	images = vmc.logical_device.get().getSwapchainImagesKHR(swapchain);

	for (const auto& image : images)
	{
		vk::ImageViewCreateInfo ivci;
		ivci.image = image;
		ivci.viewType = vk::ImageViewType::e2D;
		ivci.format = surface_format.format;
		ivci.components.r = vk::ComponentSwizzle::eIdentity;
		ivci.components.g = vk::ComponentSwizzle::eIdentity;
		ivci.components.b = vk::ComponentSwizzle::eIdentity;
		ivci.components.a = vk::ComponentSwizzle::eIdentity;
		ivci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		ivci.subresourceRange.baseMipLevel = 0;
		ivci.subresourceRange.levelCount = 1;
		ivci.subresourceRange.baseArrayLayer = 0;
		ivci.subresourceRange.layerCount = 1;
		image_views.push_back(vmc.logical_device.get().createImageView(ivci));
	}
}

vk::PresentModeKHR Swapchain::choose_present_mode(const VulkanMainContext& vmc, bool vsync)
{
	std::vector<vk::PresentModeKHR> present_modes = vmc.get_surface_present_modes();
	for (const auto& pm : present_modes)
	{
		if (vsync && pm == vk::PresentModeKHR::eFifo) return pm;
		if (!vsync && pm == vk::PresentModeKHR::eImmediate) return pm;
	}
	return vk::PresentModeKHR::eFifo;
}

vk::Extent2D Swapchain::choose_extent(const VulkanMainContext& vmc)
{
	const vk::SurfaceCapabilitiesKHR capabilities = vmc.get_surface_capabilities();
	if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
	{
		return capabilities.currentExtent;
	}
	int32_t width = 0;
	int32_t height = 0;
	SDL_GetWindowSizeInPixels(vmc.window.get(), &width, &height);
	if (width <= 0 || height <= 0)
	{
		vk::Extent2D fallback = capabilities.minImageExtent;
		fallback.width = std::max(1u, fallback.width);
		fallback.height = std::max(1u, fallback.height);
		return fallback;
	}
	vk::Extent2D chosen(width, height);
	chosen.width = std::clamp(chosen.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
	chosen.height = std::clamp(chosen.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
	return chosen;
}

vk::SurfaceFormatKHR Swapchain::choose_surface_format(const VulkanMainContext& vmc)
{
	std::vector<vk::SurfaceFormatKHR> formats = vmc.get_surface_formats();
	for (const auto& format : formats)
	{
		if (format.format == vk::Format::eB8G8R8A8Unorm && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) return format;
	}
	return formats[0];
}

vk::Format Swapchain::choose_depth_format(const VulkanMainContext& vmc)
{
	std::vector<vk::Format> candidates{vk::Format::eD24UnormS8Uint, vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint};
	for (vk::Format format : candidates)
	{
		vk::FormatProperties props = vmc.physical_device.get().getFormatProperties(format);
		if ((props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) == vk::FormatFeatureFlagBits::eDepthStencilAttachment)
		{
			return format;
		}
	}
	VKTE_THROW("vkte: Failed to find supported format!");
}
} // namespace vkte
