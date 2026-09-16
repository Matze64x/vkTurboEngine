#pragma once

#include <cstddef>
#include <span>
#include "vulkan/vulkan.hpp"
#include "vk_mem_alloc.h"

#include "vkte/memory_location.hpp"
#include "vkte/queue_families.hpp"

namespace vkte
{
class VulkanMainContext;
class Command;

struct ImageSubresourceRangeDescription
{
	vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
	uint32_t base_mip_level = 0;
	uint32_t level_count = 1;
	uint32_t base_array_layer = 0;
	uint32_t layer_count = 1;
};

struct ImageTransitionDescription
{
	vk::Image image = VK_NULL_HANDLE;
	ImageSubresourceRangeDescription range{};
	vk::ImageLayout old_layout = vk::ImageLayout::eUndefined;
	vk::ImageLayout new_layout = vk::ImageLayout::eUndefined;
	vk::PipelineStageFlags2 src_stage = vk::PipelineStageFlagBits2::eNone;
	vk::AccessFlags2 src_access = vk::AccessFlagBits2::eNone;
	vk::PipelineStageFlags2 dst_stage = vk::PipelineStageFlagBits2::eNone;
	vk::AccessFlags2 dst_access = vk::AccessFlagBits2::eNone;
	uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED;
	uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED;
};

bool has_stencil(vk::Format depth_format);
vk::ImageAspectFlags default_aspect_for_format(vk::Format format);
void perform_image_layout_transition(vk::CommandBuffer& cb, const ImageTransitionDescription& transition);
void perform_image_layout_transition(vk::CommandBuffer& cb, const std::vector<ImageTransitionDescription>& transitions);
void blit_image(vk::CommandBuffer& cb, vk::Image& src, uint32_t src_mip_map_lvl, vk::Offset3D src_offset, vk::Image& dst, uint32_t dst_mip_map_lvl, vk::Offset3D dst_offset, uint32_t layer_count);
void copy_image(vk::CommandBuffer& cb, vk::Image& src, vk::Image& dst, uint32_t width, uint32_t height, uint32_t layer_count);

class Image
{
public:
	struct Settings
	{
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t depth = 1;
		vk::Format format = vk::Format::eR8G8B8A8Unorm;
		vk::ImageUsageFlags usage_flags{};
		vk::SampleCountFlagBits sample_count = vk::SampleCountFlagBits::e1;
		bool use_mip_maps = false;
		uint32_t base_mip_map_lvl = 0;
		Queues queues{};
		vk::ImageViewType image_view_type = vk::ImageViewType::e2D;
		bool image_view_required = true;
		uint32_t layer_count = 1;
		std::vector<std::span<const std::byte>> initial_data{};
		MemoryLocation location = MemoryLocation::DeviceLocal;
	};

	Image(const VulkanMainContext& vmc, Command& command, const Settings& settings);
	~Image();
	Image(const Image&) = delete;
	Image& operator=(const Image&) = delete;
	Image(Image&&) = delete;
	Image& operator=(Image&&) = delete;
	void create_sampler(vk::Filter filter = vk::Filter::eLinear, vk::SamplerAddressMode sampler_address_mode = vk::SamplerAddressMode::eRepeat, bool enable_anisotropy = true);
	void transition_image_layout(Command& command, vk::ImageLayout new_layout, vk::PipelineStageFlags2 src_stage_flags, vk::PipelineStageFlags2 dst_stage_flags, vk::AccessFlags2 src_access_flags, vk::AccessFlags2 dst_access_flags);
	VmaAllocation get_allocation() const;
	VmaAllocationInfo get_allocation_info() const;
	vk::DeviceSize get_byte_size() const;
	uint32_t get_layer_count() const;
	vk::ImageLayout get_layout() const;
	vk::Image& get_image();
	vk::ImageView get_view() const;
	vk::Sampler get_sampler() const;

private:
	const VulkanMainContext& vmc;
	vk::Format format = vk::Format::eR8G8B8A8Unorm;
	uint32_t w, h, d;
	uint32_t mip_levels;
	uint32_t layer_count;
	vk::DeviceSize byte_size;
	vk::ImageLayout layout;
	vk::Image image;
	VmaAllocation vmaa;
	vk::ImageView view;
	vk::Sampler sampler;

	std::pair<vk::Image, VmaAllocation> create_image(Queues queues, vk::ImageUsageFlags usage, vk::SampleCountFlagBits sample_count, bool use_mip_levels, vk::Format format, vk::Extent3D extent, uint32_t layer_count, vk::ImageCreateFlags flags = {}, MemoryLocation location = MemoryLocation::DeviceLocal);
	void create_image_from_data(const std::byte* data, Command& command, Queues queues, uint32_t base_mip_map_lvl, vk::ImageUsageFlags usage_flags, vk::ImageViewType image_view_type, MemoryLocation location);
	void create_image_view(vk::ImageAspectFlags aspects, vk::ImageViewType image_view_type = vk::ImageViewType::e2D);
	void generate_mipmaps(Command& command);
};
} // namespace vkte
