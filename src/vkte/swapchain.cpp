#include "vkte/swapchain.hpp"

#include "vkte/vkte_log.hpp"
#include "vkte_window/window.hpp"

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

void Swapchain::construct(const vk::Device& device, const QueueFamilies& queue_families, const SwapchainSettings& settings, Command& command, Storage& storage)
{
	extent = settings.extent;
	surface_format = settings.surface_format;
	depth_format = settings.depth_format;
	swapchain = create_swapchain(device, queue_families, settings);
	depth_buffer = storage.add_image("depth_buffer", extent.width, extent.height, vk::ImageUsageFlagBits::eDepthStencilAttachment, depth_format, vk::SampleCountFlagBits::e1, false, 0, QueueFamilyFlags::Graphics);
	Image& depth_buffer_image = storage.get_image(depth_buffer);
	depth_buffer_image.transition_image_layout(command, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eNone, vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
	depth_image = depth_buffer_image.get_image();
	depth_view = depth_buffer_image.get_view();
	create_images(device);
}

void Swapchain::destruct(const vk::Device& device, Storage& storage)
{
	for (auto& image_view : image_views) device.destroyImageView(image_view);
	image_views.clear();
	storage.destroy(depth_buffer);
	device.destroySwapchainKHR(swapchain);
}

vk::SwapchainKHR Swapchain::create_swapchain(const vk::Device& device, const QueueFamilies& queue_families, const SwapchainSettings& settings)
{
	uint32_t image_count = settings.capabilities.maxImageCount > 0 ? std::min(settings.capabilities.minImageCount + 1, settings.capabilities.maxImageCount) : settings.capabilities.minImageCount + 1;

	vk::SwapchainCreateInfoKHR sci;
	sci.surface = settings.surface;
	sci.minImageCount = image_count;
	sci.imageFormat = settings.surface_format.format;
	sci.imageColorSpace = settings.surface_format.colorSpace;
	sci.imageExtent = settings.extent;
	sci.imageArrayLayers = 1;
	sci.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
	sci.preTransform = settings.capabilities.currentTransform;
	sci.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	sci.presentMode = settings.present_mode;
	sci.clipped = VK_TRUE;
	sci.oldSwapchain = VK_NULL_HANDLE;
	std::vector<uint32_t> queue_family_indices = queue_families.get(QueueFamilyFlags::Graphics | QueueFamilyFlags::Present);
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
	return device.createSwapchainKHR(sci);
}

void Swapchain::create_images(const vk::Device& device)
{
	images = device.getSwapchainImagesKHR(swapchain);

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
		image_views.push_back(device.createImageView(ivci));
	}
}

SwapchainSettings Swapchain::choose_settings(const PhysicalDevice& physical_device, const Window& window, vk::SurfaceKHR surface, bool vsync)
{
	SwapchainSettings settings;
	settings.surface = surface;
	settings.capabilities = physical_device.get().getSurfaceCapabilitiesKHR(surface);

	if (settings.capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
	{
		settings.extent = settings.capabilities.currentExtent;
	}
	else
	{
		vk::Extent2D pixel_size = window.get_pixel_size();
		if (pixel_size.width == 0 || pixel_size.height == 0)
		{
			settings.extent = settings.capabilities.minImageExtent;
			settings.extent.width = std::max(1u, settings.extent.width);
			settings.extent.height = std::max(1u, settings.extent.height);
		}
		else
		{
			settings.extent = pixel_size;
			settings.extent.width = std::clamp(settings.extent.width, settings.capabilities.minImageExtent.width, settings.capabilities.maxImageExtent.width);
			settings.extent.height = std::clamp(settings.extent.height, settings.capabilities.minImageExtent.height, settings.capabilities.maxImageExtent.height);
		}
	}

	std::vector<vk::SurfaceFormatKHR> formats = physical_device.get().getSurfaceFormatsKHR(surface);
	settings.surface_format = formats[0];
	for (const vk::SurfaceFormatKHR& format : formats)
	{
		if (format.format == vk::Format::eB8G8R8A8Unorm && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
		{
			settings.surface_format = format;
			break;
		}
	}

	std::vector<vk::Format> depth_candidates{vk::Format::eD24UnormS8Uint, vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint};
	bool depth_format_found = false;
	for (vk::Format format : depth_candidates)
	{
		vk::FormatProperties props = physical_device.get().getFormatProperties(format);
		if ((props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) == vk::FormatFeatureFlagBits::eDepthStencilAttachment)
		{
			settings.depth_format = format;
			depth_format_found = true;
			break;
		}
	}
	VKTE_ASSERT(depth_format_found, "vkte: Failed to find supported depth format!");

	settings.present_mode = vk::PresentModeKHR::eFifo;
	for (const vk::PresentModeKHR& pm : physical_device.get().getSurfacePresentModesKHR(surface))
	{
		if (vsync && pm == vk::PresentModeKHR::eFifo) { settings.present_mode = pm; break; }
		if (!vsync && pm == vk::PresentModeKHR::eImmediate) { settings.present_mode = pm; break; }
	}

	return settings;
}
} // namespace vkte
