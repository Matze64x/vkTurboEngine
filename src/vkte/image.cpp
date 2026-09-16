#include "vkte/image.hpp"

#include <cmath>
#include <cstring>
#include <format>
#include "vkte/buffer.hpp"
#include "vkte/command.hpp"
#include "vkte/vkte_log.hpp"
#include "vkte/vulkan_main_context.hpp"

namespace vkte
{
static constexpr uint32_t bytes_per_pixel = 4;

static vk::ImageCreateFlags get_image_create_flags(vk::ImageViewType view_type)
{
	return (view_type == vk::ImageViewType::eCube || view_type == vk::ImageViewType::eCubeArray) ? vk::ImageCreateFlags(vk::ImageCreateFlagBits::eCubeCompatible) : vk::ImageCreateFlags{};
}

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

void perform_image_layout_transition(vk::CommandBuffer& cb, const ImageTransitionDescription& t)
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

void perform_image_layout_transition(vk::CommandBuffer& cb, const std::vector<ImageTransitionDescription>& transitions)
{
	std::vector<vk::ImageMemoryBarrier2> barriers;
	barriers.reserve(transitions.size());
	for (const ImageTransitionDescription& t : transitions)
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

Image::Image(const VulkanMainContext& vmc, Command& command, const Settings& settings) : vmc(vmc), format(settings.format), w(settings.width), h(settings.height), d(settings.depth), mip_levels(settings.use_mip_maps ? uint32_t(std::floor(std::log2(std::max({settings.width, settings.height, settings.depth})))) + 1 : 1), layer_count(settings.depth > 1 || settings.initial_data.empty() ? settings.layer_count : uint32_t(settings.initial_data.size()))
{
	VKTE_ASSERT(settings.width > 0 && settings.height > 0 && settings.depth > 0, "vkte: Image::Settings::width/height/depth must not be 0");
	VKTE_ASSERT(settings.usage_flags, "vkte: Image::Settings::usage_flags must not be empty");
	VKTE_ASSERT(settings.depth == 1 || settings.layer_count == 1, "vkte: Image::Settings::depth and layer_count are mutually exclusive -- a 3D image cannot also be an array");
	VKTE_ASSERT(layer_count > 0, "vkte: Image::Settings::layer_count must not be 0 (and initial_data, if given, must not be empty)");

	if (settings.depth > 1)
	{
		VKTE_ASSERT(settings.image_view_type == vk::ImageViewType::e3D, "vkte: Image::Settings::image_view_type must be e3D when depth > 1");
		VKTE_ASSERT(settings.base_mip_map_lvl == 0, "vkte: Image::Settings::base_mip_map_lvl > 0 is not supported for a 3D image (depth > 1)");
		VKTE_ASSERT(!settings.use_mip_maps || settings.initial_data.empty(), "vkte: Auto-generating mipmaps for an uploaded 3D image is not supported (mip generation is 2D-only); create it without use_mip_maps and populate the mips yourself if needed");
	}
	else if (layer_count > 1)
	{
		const bool is_cube_view = settings.image_view_type == vk::ImageViewType::eCube || settings.image_view_type == vk::ImageViewType::eCubeArray;
		VKTE_ASSERT(settings.image_view_type == vk::ImageViewType::e2DArray || is_cube_view, "vkte: Image::Settings::image_view_type must be e2DArray, eCube, or eCubeArray when layer_count > 1");
		if (is_cube_view)
		{
			VKTE_ASSERT(layer_count % 6 == 0, "vkte: A cube/cube array Image needs layer_count to be a multiple of 6");
			VKTE_ASSERT(settings.width == settings.height, "vkte: A cube/cube array Image needs width == height");
		}
	}

	byte_size = vk::DeviceSize(w) * h * d * bytes_per_pixel * layer_count;

	if (settings.initial_data.empty())
	{
		std::tie(image, vmaa) = create_image(settings.queues, settings.usage_flags, settings.sample_count, settings.use_mip_maps, format, vk::Extent3D(w, h, d), layer_count, get_image_create_flags(settings.image_view_type), settings.location);
		layout = vk::ImageLayout::eUndefined;
		if (settings.image_view_required) create_image_view(default_aspect_for_format(format), settings.image_view_type);
	}
	else
	{
		VKTE_ASSERT(settings.depth == 1 || settings.initial_data.size() == settings.depth, "vkte: Image::Settings::initial_data must have exactly `depth` entries (one per Z-slice) for a 3D image");

		const std::size_t expected_entry_byte_size = std::size_t(w) * h * bytes_per_pixel;
		for (const std::span<const std::byte>& entry_data : settings.initial_data)
		{
			VKTE_ASSERT(entry_data.size() == expected_entry_byte_size, std::format("vkte: Image::Settings::initial_data entry has {} bytes, expected width*height*4 = {}", entry_data.size(), expected_entry_byte_size));
		}

		std::vector<std::byte> concatenated_data(byte_size);
		std::size_t offset = 0;
		for (const std::span<const std::byte>& entry_data : settings.initial_data)
		{
			std::memcpy(concatenated_data.data() + offset, entry_data.data(), entry_data.size());
			offset += entry_data.size();
		}
		create_image_from_data(concatenated_data.data(), command, settings.queues, settings.base_mip_map_lvl, settings.usage_flags, settings.image_view_type, settings.location);
	}
}

Image::~Image()
{
	vmc.logical_device.get().destroySampler(sampler);
	vmc.logical_device.get().destroyImageView(view);
	vmaDestroyImage(vmc.va, VkImage(image), vmaa);
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

std::pair<vk::Image, VmaAllocation> Image::create_image(Queues queues, vk::ImageUsageFlags usage, vk::SampleCountFlagBits sample_count, bool use_mip_levels, vk::Format format, vk::Extent3D extent, uint32_t layer_count, vk::ImageCreateFlags flags, MemoryLocation location)
{
	std::vector<uint32_t> queue_family_indices = vmc.queue_families.get(queues);
	uint32_t mip_levels = use_mip_levels ? uint32_t(std::floor(std::log2(std::max({extent.width, extent.height, extent.depth})))) + 1 : 1;
	if (mip_levels > 1) usage |= vk::ImageUsageFlagBits::eTransferSrc;
	const vk::ImageType image_type = extent.depth > 1 ? vk::ImageType::e3D : vk::ImageType::e2D;

	auto try_create = [&](MemoryLocation candidate_location) -> std::optional<std::pair<vk::Image, VmaAllocation>> {
		vk::ImageCreateInfo ici;
		ici.imageType = image_type;
		ici.extent = extent;
		ici.mipLevels = mip_levels;
		ici.arrayLayers = layer_count;
		ici.format = format;
		ici.tiling = candidate_location == MemoryLocation::HostVisible ? vk::ImageTiling::eLinear : vk::ImageTiling::eOptimal;
		ici.initialLayout = vk::ImageLayout::eUndefined;
		ici.usage = usage;
		ici.sharingMode = queue_family_indices.size() == 1 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent;
		ici.queueFamilyIndexCount = queue_family_indices.size();
		ici.pQueueFamilyIndices = queue_family_indices.data();
		ici.samples = sample_count;
		ici.flags = flags;

		const VmaAllocationCreateInfo vaci = to_vma_allocation_create_info(candidate_location);
		std::pair<vk::Image, VmaAllocation> result;
		if (vmaCreateImage(vmc.va, (VkImageCreateInfo*) (&ici), &vaci, (VkImage*) (&result.first), &result.second, nullptr) != VK_SUCCESS) return std::nullopt;
		return result;
	};

	if (std::optional<std::pair<vk::Image, VmaAllocation>> result = try_create(location)) return *result;
	const std::optional<MemoryLocation> fallback = get_fallback_location(location);
	VKTE_ASSERT(fallback.has_value(), "vkte: Failed to allocate image in the requested MemoryLocation");
	std::optional<std::pair<vk::Image, VmaAllocation>> fallback_result = try_create(*fallback);
	VKTE_ASSERT(fallback_result.has_value(), "vkte: Failed to allocate image in its fallback MemoryLocation as well");
	return *fallback_result;
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

void Image::create_image_from_data(const std::byte* data, Command& command, Queues queues, uint32_t base_mip_map_lvl, vk::ImageUsageFlags usage_flags, vk::ImageViewType image_view_type, MemoryLocation location)
{
	Buffer::Settings staging_settings;
	staging_settings.initial_data = std::as_bytes(std::span(data, byte_size));
	staging_settings.usage_flags = vk::BufferUsageFlagBits::eTransferSrc;
	staging_settings.location = MemoryLocation::HostVisible;
	staging_settings.queues = QueueFamilyFlags::Transfer;
	Buffer buffer(vmc, command, staging_settings);

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
		copy_buffer_to_image(command, buffer, vk::Extent3D(w, h, d), image, layer_count, bytes_per_pixel);
	};

	const vk::ImageCreateFlags image_create_flags = get_image_create_flags(image_view_type);

	// check if image should start at base_mip_map_lvl to save some storage
	// create image with original resolution and copy to actual image with reduced resolution
	if (base_mip_map_lvl > 0)
	{
		auto [tmp_image, tmp_alloc] = create_image(QueueFamilyFlags::Graphics | QueueFamilyFlags::Transfer, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc, vk::SampleCountFlagBits::e1, false, format, vk::Extent3D(w, h, d), layer_count);
		move_buffer_to_image(tmp_image, 1);

		vk::Offset3D tmp_image_offset(w, h, 1);
		mip_levels -= base_mip_map_lvl;
		w = std::max(1u, uint32_t(w / std::pow(2, base_mip_map_lvl)));
		h = std::max(1u, uint32_t(h / std::pow(2, base_mip_map_lvl)));
		byte_size = vk::DeviceSize(w) * h * bytes_per_pixel * layer_count;

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
		std::tie(image, vmaa) = create_image(queues, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | usage_flags, vk::SampleCountFlagBits::e1, true, format, vk::Extent3D(w, h, d), layer_count, image_create_flags, location);
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
		blit_image(cb, tmp_image, 0, tmp_image_offset, image, 0, {int32_t(w), int32_t(h), 1}, layer_count);
		command.submit_graphics(cb, true);

		vmaDestroyImage(vmc.va, VkImage(tmp_image), tmp_alloc);
	}
	else
	{
		// layout of image is transitioned in move_buffer_to_image
		std::tie(image, vmaa) = create_image(queues, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | usage_flags, vk::SampleCountFlagBits::e1, true, format, vk::Extent3D(w, h, d), layer_count, image_create_flags, location);
		move_buffer_to_image(image, mip_levels);
	}
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
	ivci.viewType = image_view_type;
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

	ImageSubresourceRangeDescription range{
		.aspect = vk::ImageAspectFlagBits::eColor,
		.base_mip_level = 0,
		.level_count = 1,
		.base_array_layer = 0,
		.layer_count = layer_count
	};

	uint32_t mip_w = w;
	uint32_t mip_h = h;
	for (uint32_t i = 1; i < mip_levels; ++i)
	{
		range.base_mip_level = i - 1;

		perform_image_layout_transition(cb, {
			.image = image,
			.range = range,
			.old_layout = vk::ImageLayout::eTransferDstOptimal,
			.new_layout = vk::ImageLayout::eTransferSrcOptimal,
			.src_stage = vk::PipelineStageFlagBits2::eTransfer,
			.src_access = vk::AccessFlagBits2::eTransferWrite,
			.dst_stage = vk::PipelineStageFlagBits2::eTransfer,
			.dst_access = vk::AccessFlagBits2::eTransferRead
		});

		blit_image(cb, image, i - 1, {int32_t(mip_w), int32_t(mip_h), 1}, image, i, {int32_t(mip_w > 1 ? mip_w / 2 : 1), int32_t(mip_h > 1 ? mip_h / 2 : 1), 1}, layer_count);

		perform_image_layout_transition(cb, {
			.image = image,
			.range = range,
			.old_layout = vk::ImageLayout::eTransferSrcOptimal,
			.new_layout = vk::ImageLayout::eShaderReadOnlyOptimal,
			.src_stage = vk::PipelineStageFlagBits2::eTransfer,
			.src_access = vk::AccessFlagBits2::eTransferRead,
			.dst_stage = vk::PipelineStageFlagBits2::eFragmentShader,
			.dst_access = vk::AccessFlagBits2::eShaderRead
		});

		if (mip_w > 1) mip_w /= 2;
		if (mip_h > 1) mip_h /= 2;
	}

	range.base_mip_level = mip_levels - 1;
	perform_image_layout_transition(cb, {
		.image = image,
		.range = range,
		.old_layout = vk::ImageLayout::eTransferDstOptimal,
		.new_layout = vk::ImageLayout::eShaderReadOnlyOptimal,
		.src_stage = vk::PipelineStageFlagBits2::eTransfer,
		.src_access = vk::AccessFlagBits2::eTransferWrite,
		.dst_stage = vk::PipelineStageFlagBits2::eFragmentShader,
		.dst_access = vk::AccessFlagBits2::eShaderRead
	});
	layout = vk::ImageLayout::eShaderReadOnlyOptimal;

	command.submit_graphics(cb, true);
}
} // namespace vkte
