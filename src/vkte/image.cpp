#include "vkte/image.hpp"

#include <cmath>
#include "vkte/buffer.hpp"

namespace vkte
{
bool has_stencil(vk::Format depth_format)
{
	return depth_format == vk::Format::eD24UnormS8Uint || depth_format == vk::Format::eD32SfloatS8Uint;
}

vk::ImageAspectFlags default_aspect_for_format(vk::Format format)
{
	switch (format)
	{
		case vk::Format::eD16Unorm:
		case vk::Format::eD32Sfloat:
			return vk::ImageAspectFlagBits::eDepth;
		case vk::Format::eS8Uint:
			return vk::ImageAspectFlagBits::eStencil;
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eD32SfloatS8Uint:
			return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
		default:
			return vk::ImageAspectFlagBits::eColor;
	}
}

void perform_image_layout_transition(vk::CommandBuffer& cb, const ImageTransitionDesc& t)
{
	vk::ImageMemoryBarrier2 b;
	b.srcStageMask = t.src_stage;
	b.srcAccessMask = t.src_access;
	b.dstStageMask = t.dst_stage;
	b.dstAccessMask = t.dst_access;
	b.oldLayout = t.old_layout;
	b.newLayout = t.new_layout;
	b.srcQueueFamilyIndex = t.src_queue_family;
	b.dstQueueFamilyIndex = t.dst_queue_family;
	b.image = t.image;
	b.subresourceRange.aspectMask = t.range.aspect;
	b.subresourceRange.baseMipLevel = t.range.base_mip_level;
	b.subresourceRange.levelCount = t.range.level_count;
	b.subresourceRange.baseArrayLayer = t.range.base_array_layer;
	b.subresourceRange.layerCount = t.range.layer_count;
	vk::DependencyInfo dep;
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &b;
	cb.pipelineBarrier2(dep);
}

void perform_image_layout_transition(vk::CommandBuffer& cb, const std::vector<ImageTransitionDesc>& transitions)
{
	std::vector<vk::ImageMemoryBarrier2> barriers;
	barriers.reserve(transitions.size());
	for (const ImageTransitionDesc& t : transitions)
	{
		vk::ImageMemoryBarrier2 b;
		b.srcStageMask = t.src_stage;
		b.srcAccessMask = t.src_access;
		b.dstStageMask = t.dst_stage;
		b.dstAccessMask = t.dst_access;
		b.oldLayout = t.old_layout;
		b.newLayout = t.new_layout;
		b.srcQueueFamilyIndex = t.src_queue_family;
		b.dstQueueFamilyIndex = t.dst_queue_family;
		b.image = t.image;
		b.subresourceRange.aspectMask = t.range.aspect;
		b.subresourceRange.baseMipLevel = t.range.base_mip_level;
		b.subresourceRange.levelCount = t.range.level_count;
		b.subresourceRange.baseArrayLayer = t.range.base_array_layer;
		b.subresourceRange.layerCount = t.range.layer_count;
		barriers.push_back(b);
	}
	vk::DependencyInfo dep;
	dep.imageMemoryBarrierCount = barriers.size();
	dep.pImageMemoryBarriers = barriers.data();
	cb.pipelineBarrier2(dep);
}

Image::Image(const VulkanMainContext& vmc, Command& command, const unsigned char* data, uint32_t width, uint32_t height, bool use_mip_maps, uint32_t base_mip_map_lvl, Queues queues, vk::ImageUsageFlags usage_flags) : vmc(vmc), w(width), h(height), c(4), byte_size(width * height * 4), mip_levels(use_mip_maps ? std::floor(std::log2(std::max(w, h))) + 1 : 1), layer_count(1)
{
	create_image_from_data(data, command, queues, base_mip_map_lvl, usage_flags);
}

Image::Image(const VulkanMainContext& vmc, Command& command, const std::vector<std::vector<unsigned char>>& data, uint32_t width, uint32_t height, bool use_mip_maps, uint32_t base_mip_map_lvl, Queues queues, vk::ImageUsageFlags usage_flags, vk::ImageViewType image_view_type) : vmc(vmc), w(width), h(height), c(4), byte_size(width * height * 4 * data.size()), mip_levels(use_mip_maps ? std::floor(std::log2(std::max(w, h))) + 1 : 1), layer_count(data.size())
{
	std::vector<unsigned char> copy_data;
	for (const auto& i : data)
	{
		for (const auto& j : i) copy_data.push_back(j);
	}
	create_image_from_data(copy_data.data(), command, queues, base_mip_map_lvl, usage_flags, image_view_type);
}

Image::Image(const VulkanMainContext& vmc, const Command& command, uint32_t width, uint32_t height, vk::ImageUsageFlags usage, vk::Format format, vk::SampleCountFlagBits sample_count, bool use_mip_maps, uint32_t base_mip_map_lvl, Queues queues, bool image_view_required, uint32_t layer_count) : vmc(vmc), format(format), w(width), h(height), c(4), mip_levels(use_mip_maps ? std::floor(std::log2(std::max(w, h))) + 1 : 1), layer_count(layer_count)
{
	std::tie(image, vmaa) = create_image(queues, usage, sample_count, use_mip_maps, format, vk::Extent3D(w, h, 1), layer_count, vmc.va, !image_view_required);
	layout = vk::ImageLayout::eUndefined;
	if(image_view_required) create_image_view(default_aspect_for_format(format));
}

void blit_image(vk::CommandBuffer& cb, vk::Image& src, uint32_t src_mip_map_lvl, vk::Offset3D src_offset, vk::Image& dst, uint32_t dst_mip_map_lvl, vk::Offset3D dst_offset, uint32_t layer_count)
{
	vk::ImageBlit blit{};
	blit.srcOffsets[0] = vk::Offset3D(0, 0, 0);
	blit.srcOffsets[1] = src_offset;
	blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	blit.srcSubresource.mipLevel = src_mip_map_lvl;
	blit.srcSubresource.baseArrayLayer = 0;
	blit.srcSubresource.layerCount = layer_count;
	blit.dstOffsets[0] = vk::Offset3D(0, 0, 0);
	blit.dstOffsets[1] = dst_offset;
	blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	blit.dstSubresource.mipLevel = dst_mip_map_lvl;
	blit.dstSubresource.baseArrayLayer = 0;
	blit.dstSubresource.layerCount = layer_count;
	cb.blitImage(src, vk::ImageLayout::eTransferSrcOptimal, dst, vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear);
}

void copy_image(vk::CommandBuffer& cb, vk::Image& src, vk::Image& dst, uint32_t width, uint32_t height, uint32_t layer_count)
{
	vk::ImageCopy ic{};
	ic.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	ic.srcSubresource.layerCount = layer_count;
	ic.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	ic.dstSubresource.layerCount = layer_count;
	ic.extent.width = width;
	ic.extent.height = height;
	ic.extent.depth = 1;

	cb.copyImage(src, vk::ImageLayout::eTransferSrcOptimal, dst, vk::ImageLayout::eTransferDstOptimal, 1, &ic);
}

std::pair<vk::Image, VmaAllocation> Image::create_image(Queues queues, vk::ImageUsageFlags usage, vk::SampleCountFlagBits sample_count, bool use_mip_levels, vk::Format format, vk::Extent3D extent, uint32_t layer_count, const VmaAllocator& va, bool host_visible)
{
	std::vector<uint32_t> queue_family_indices = vmc.queue_families.get(queues);
	uint32_t mip_levels = use_mip_levels ? std::floor(std::log2(std::max(extent.width, extent.height))) + 1 : 1;
	if (mip_levels > 1) usage |= vk::ImageUsageFlagBits::eTransferSrc;
	vk::ImageCreateInfo ici;
	ici.imageType = vk::ImageType::e2D;
	ici.extent = extent;
	ici.extent.depth = 1;
	ici.mipLevels = mip_levels;
	ici.arrayLayers = layer_count;
	ici.format = format;
	ici.tiling = host_visible ? vk::ImageTiling::eLinear : vk::ImageTiling::eOptimal;
	ici.initialLayout = vk::ImageLayout::eUndefined;
	ici.usage = usage;
	ici.sharingMode = queue_family_indices.size() == 1 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent;
	ici.queueFamilyIndexCount = queue_family_indices.size();
	ici.pQueueFamilyIndices = queue_family_indices.data();
	ici.samples = sample_count;
	ici.flags = {};

	std::pair<vk::Image, VmaAllocation> image;
	VmaAllocationCreateInfo vaci{};
	if (host_visible)
	{
		vaci.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
	}
	else
	{
		vaci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		vaci.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		vaci.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
	}
	vmaCreateImage(va, (VkImageCreateInfo*) (&ici), &vaci, (VkImage*) (&image.first), &image.second, nullptr);
	return image;
}

void copy_buffer_to_image(Command& command, const Buffer& buffer, vk::Extent3D extent, vk::Image image, uint32_t layer_count, uint32_t pixel_byte_size)
{
	vk::CommandBuffer& cb = command.get_one_time_transfer_buffer();
	std::vector<vk::BufferImageCopy> copy_regions;
	for (uint32_t i = 0; i < layer_count; ++i)
	{
		vk::BufferImageCopy copy_region{};
		copy_region.bufferOffset = i * extent.width * extent.height * pixel_byte_size;
		copy_region.bufferRowLength = 0;
		copy_region.bufferImageHeight = 0;
		copy_region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		copy_region.imageSubresource.mipLevel = 0;
		copy_region.imageSubresource.baseArrayLayer = i;
		copy_region.imageSubresource.layerCount = 1;
		copy_region.imageOffset = vk::Offset3D{0, 0, 0};
		copy_region.imageExtent = extent;
		copy_regions.push_back(copy_region);
	}

	cb.copyBufferToImage(buffer.get(), image, vk::ImageLayout::eTransferDstOptimal, copy_regions);
	command.submit_transfer(cb, true);
}

void Image::create_image_from_data(const unsigned char* data, Command& command, Queues queues, uint32_t base_mip_map_lvl, vk::ImageUsageFlags usage_flags, vk::ImageViewType image_view_type)
{
	Buffer buffer(vmc, command, data, byte_size, vk::BufferUsageFlagBits::eTransferSrc, false, QueueFamilyFlags::Transfer);

	vk::FormatProperties format_properties = vmc.physical_device.get().getFormatProperties(format);
	if (!(format_properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear))
	{
		mip_levels = 1;
		base_mip_map_lvl = 0;
	}

	auto move_buffer_to_image = [&](vk::Image image, uint32_t mip_levels) -> void {
		// copy image data to tmp_image
		vk::CommandBuffer& cb = command.get_one_time_transfer_buffer();
		perform_image_layout_transition(cb, {
			.image = image,
			.range = {
				.aspect = vk::ImageAspectFlagBits::eColor,
				.base_mip_level = 0,
				.level_count = mip_levels,
				.base_array_layer = 0,
				.layer_count = layer_count
			},
			.old_layout = vk::ImageLayout::eUndefined,
			.new_layout = vk::ImageLayout::eTransferDstOptimal,
			.src_stage = vk::PipelineStageFlagBits2::eTransfer,
			.src_access = vk::AccessFlagBits2::eNone,
			.dst_stage = vk::PipelineStageFlagBits2::eTransfer,
			.dst_access = vk::AccessFlagBits2::eTransferWrite
		});
		command.submit_transfer(cb, true);
		copy_buffer_to_image(command, buffer, vk::Extent3D(w, h, 1), image, layer_count, c);
	};

	// check if image should start at base_mip_map_lvl to save some storage
	// create image with original resolution and copy to actual image with reduced resolution
	if (base_mip_map_lvl > 0)
	{
		auto [tmp_image, tmp_alloc] = create_image(QueueFamilyFlags::Graphics | QueueFamilyFlags::Transfer, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc, vk::SampleCountFlagBits::e1, false, format, vk::Extent3D(w, h, 1), layer_count, vmc.va);
		move_buffer_to_image(tmp_image, 1);

		vk::Offset3D tmp_image_offset(w, h, 1);
		mip_levels -= base_mip_map_lvl;
		w = std::max(1.0, w / (std::pow(2, base_mip_map_lvl)));
		h = std::max(1.0, h / (std::pow(2, base_mip_map_lvl)));
		byte_size = w * h * 4;

		// create image with reduced resolution by blitting
		vk::CommandBuffer& cb = command.get_one_time_graphics_buffer();
		perform_image_layout_transition(cb, {
			.image = tmp_image,
			.range = {
				.aspect = vk::ImageAspectFlagBits::eColor,
				.base_mip_level = 0,
				.level_count = 1,
				.base_array_layer = 0,
				.layer_count = layer_count
			},
			.old_layout = vk::ImageLayout::eTransferDstOptimal,
			.new_layout = vk::ImageLayout::eTransferSrcOptimal,
			.src_stage = vk::PipelineStageFlagBits2::eTransfer,
			.src_access = vk::AccessFlagBits2::eTransferWrite,
			.dst_stage = vk::PipelineStageFlagBits2::eTransfer,
			.dst_access = vk::AccessFlagBits2::eTransferRead
		});
		std::tie(image, vmaa) = create_image(queues, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | usage_flags, vk::SampleCountFlagBits::e1, true, format, vk::Extent3D(w, h, 1), layer_count, vmc.va);
		perform_image_layout_transition(cb, {
			.image = image,
			.range = {
				.aspect = vk::ImageAspectFlagBits::eColor,
				.base_mip_level = 0,
				.level_count = mip_levels,
				.base_array_layer = 0,
				.layer_count = layer_count
			},
			.old_layout = vk::ImageLayout::eUndefined,
			.new_layout = vk::ImageLayout::eTransferDstOptimal,
			.src_stage = vk::PipelineStageFlagBits2::eTransfer,
			.src_access = vk::AccessFlagBits2::eNone,
			.dst_stage = vk::PipelineStageFlagBits2::eTransfer,
			.dst_access = vk::AccessFlagBits2::eTransferWrite
		});
		blit_image(cb, tmp_image, 0, tmp_image_offset, image, 0, {w, h, 1}, layer_count);
		command.submit_graphics(cb, true);

		vmaDestroyImage(vmc.va, VkImage(tmp_image), tmp_alloc);
	}
	else
	{
		// layout of image is transitioned in move_buffer_to_image
		std::tie(image, vmaa) = create_image(queues, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | usage_flags, vk::SampleCountFlagBits::e1, true, format, vk::Extent3D(w, h, 1), layer_count, vmc.va);
		move_buffer_to_image(image, mip_levels);
	}
	buffer.destruct();
	// set current layout of this image
	layout = vk::ImageLayout::eTransferDstOptimal;
	if (usage_flags & vk::ImageUsageFlagBits::eSampled)
	{
		mip_levels > 1 ? generate_mipmaps(command) : transition_image_layout(command, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eTransfer, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eTransferWrite, vk::AccessFlagBits2::eShaderRead);
	}
	create_image_view(vk::ImageAspectFlagBits::eColor, image_view_type);
	create_sampler();
}

void Image::create_image_view(vk::ImageAspectFlags aspects, vk::ImageViewType image_view_type)
{
	vk::ImageViewCreateInfo ivci;
	ivci.image = image;
	ivci.viewType = layer_count > 1 ? vk::ImageViewType::e2DArray : image_view_type;
	ivci.format = format;
	ivci.subresourceRange.aspectMask = aspects;
	ivci.subresourceRange.baseMipLevel = 0;
	ivci.subresourceRange.levelCount = mip_levels;
	ivci.subresourceRange.baseArrayLayer = 0;
	ivci.subresourceRange.layerCount = layer_count;
	view = vmc.logical_device.get().createImageView(ivci);
}

void Image::create_sampler(vk::Filter filter, vk::SamplerAddressMode sampler_address_mode, bool enable_anisotropy)
{
	vk::SamplerCreateInfo sci;
	sci.magFilter = filter;
	sci.minFilter = filter;
	sci.addressModeU = sampler_address_mode;
	sci.addressModeV = sampler_address_mode;
	sci.addressModeW = sampler_address_mode;
	if (enable_anisotropy)
	{
		sci.anisotropyEnable = VK_TRUE;
		sci.maxAnisotropy = std::min(8.0f, vmc.physical_device.get().getProperties().limits.maxSamplerAnisotropy);
	}
	else
	{
		sci.anisotropyEnable = VK_FALSE;
		sci.maxAnisotropy = 1.0f;
	}
	sci.borderColor = vk::BorderColor::eIntOpaqueBlack;
	sci.unnormalizedCoordinates = VK_FALSE;
	sci.compareEnable = VK_FALSE;
	sci.compareOp = vk::CompareOp::eAlways;
	sci.mipmapMode = format == vk::Format::eR32Sint ? vk::SamplerMipmapMode::eNearest : vk::SamplerMipmapMode::eLinear;
	sci.mipLodBias = 0.0f;
	sci.minLod = 0.0f;
	sci.maxLod = mip_levels;
	sampler = vmc.logical_device.get().createSampler(sci);
}

void Image::destruct()
{
	vmc.logical_device.get().destroySampler(sampler);
	vmc.logical_device.get().destroyImageView(view);
	vmaDestroyImage(vmc.va, VkImage(image), vmaa);
}

void Image::transition_image_layout(Command& command, vk::ImageLayout new_layout, vk::PipelineStageFlags2 src_stage_flags, vk::PipelineStageFlags2 dst_stage_flags, vk::AccessFlags2 src_access_flags, vk::AccessFlags2 dst_access_flags)
{
	// transition the image layout of this image
	vk::CommandBuffer& cb = command.get_one_time_graphics_buffer();
	perform_image_layout_transition(cb, {
		.image = image,
		.range = {
			.aspect = default_aspect_for_format(format),
			.base_mip_level = 0,
			.level_count = mip_levels,
			.base_array_layer = 0,
			.layer_count = layer_count
		},
		.old_layout = layout,
		.new_layout = new_layout,
		.src_stage = src_stage_flags,
		.src_access = src_access_flags,
		.dst_stage = dst_stage_flags,
		.dst_access = dst_access_flags
	});
	command.submit_graphics(cb, true);
	layout = new_layout;
}

VmaAllocation Image::get_allocation() const
{
	return vmaa;
}

VmaAllocationInfo Image::get_allocation_info() const
{
	VmaAllocationInfo alloc_info;
	vmaGetAllocationInfo(vmc.va, vmaa, &alloc_info);
	return alloc_info;
}

vk::DeviceSize Image::get_byte_size() const
{
	return byte_size;
}

uint32_t Image::get_layer_count() const
{
	return layer_count;
}

vk::ImageLayout Image::get_layout() const
{
	return layout;
}

vk::Image& Image::get_image()
{
	return image;
}

vk::ImageView Image::get_view() const
{
	return view;
}

vk::Sampler Image::get_sampler() const
{
	return sampler;
}

void Image::generate_mipmaps(Command& command)
{
	vk::CommandBuffer& cb = command.get_one_time_graphics_buffer();
	vk::ImageMemoryBarrier imb;
	imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imb.image = image;
	imb.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
	imb.subresourceRange.levelCount = 1;
	imb.subresourceRange.baseArrayLayer = 0;
	imb.subresourceRange.layerCount = layer_count;

	uint32_t mip_w = w;
	uint32_t mip_h = h;
	for (uint32_t i = 1; i < mip_levels; ++i)
	{
		imb.subresourceRange.baseMipLevel = i - 1;
		imb.oldLayout = vk::ImageLayout::eTransferDstOptimal;
		imb.newLayout = vk::ImageLayout::eTransferSrcOptimal;
		imb.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		imb.dstAccessMask = vk::AccessFlagBits::eTransferRead;
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr, imb);

		blit_image(cb, image, i - 1, {int32_t(mip_w), int32_t(mip_h), 1}, image, i, {int32_t(mip_w > 1 ? mip_w / 2 : 1), int32_t(mip_h > 1 ? mip_h / 2 : 1), 1}, layer_count);

		imb.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
		imb.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		imb.srcAccessMask = vk::AccessFlagBits::eTransferRead;
		imb.dstAccessMask = vk::AccessFlagBits::eShaderRead;
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr, imb);

		if (mip_w > 1) mip_w /= 2;
		if (mip_h > 1) mip_h /= 2;
	}

	imb.subresourceRange.baseMipLevel = mip_levels - 1;
	imb.oldLayout = vk::ImageLayout::eTransferDstOptimal;
	imb.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	layout = imb.newLayout;
	imb.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	imb.dstAccessMask = vk::AccessFlagBits::eShaderRead;
	cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr, imb);

	command.submit_graphics(cb, true);
}
} // namespace vkte
