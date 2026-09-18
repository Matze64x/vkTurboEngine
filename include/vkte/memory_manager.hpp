#pragma once

#include <array>
#include <cstdint>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>
#include "vulkan/vulkan.hpp"
#include "vkte/global_constants.hpp"
#include "vkte/resource_handles.hpp"
#include "vkte/storage.hpp"

namespace vkte
{
class Engine;

class MemoryManager
{
public:
	BufferHandle add_bindless_buffer(const std::string& descriptive_name, const Buffer::Settings& settings, const std::string& unique_name = "");
	ImageHandle add_bindless_image(const std::string& descriptive_name, const Image::Settings& settings, const std::string& unique_name = "");
	void destroy(const BufferHandle& handle);
	void destroy(const ImageHandle& handle);
	Buffer& get_buffer(const BufferHandle& handle);
	Image& get_image(const ImageHandle& handle);
	BufferHandle get_bindless_buffer(const std::string& unique_name);
	ImageHandle get_bindless_image(const std::string& unique_name);
	BufferHandle register_buffer(const ResourceHandle& buffer, const std::string& unique_name = "");
	ImageHandle register_image(const ResourceHandle& image, const std::string& unique_name = "");
	void unregister_buffer(const BufferHandle& handle);
	void unregister_image(const ImageHandle& handle);
	Storage& get_storage() { return storage; }

private:
	friend class Engine;
	MemoryManager(const VulkanMainContext& vmc, Storage& storage);
	void construct();
	void destruct();
	void flush_pending_destructions(bool force = false);
	const vk::DescriptorSetLayout& get_bindless_set_layout() const { return bindless_set_layout; }
	void begin_frame(uint32_t frame_index);
	void bind_bindless_set(vk::CommandBuffer cb, vk::PipelineBindPoint bind_point, const vk::PipelineLayout& pipeline_layout, uint32_t frame_index) const;

	static constexpr uint32_t max_bindless_images = 65536;
	static constexpr uint32_t max_bindless_buffers = 65536;

	void mark_buffer_dirty(uint32_t index);
	void mark_image_dirty(uint32_t index);
	uint64_t resolve_buffer_address(const ResourceHandle& resource);

	struct PendingDestruction
	{
		PendingDestruction(ResourceHandle handle, uint64_t requested_at_frame, BufferHandle bindless_buffer) : handle(handle), requested_at_frame(requested_at_frame), bindless_buffer(bindless_buffer)
		{}
		PendingDestruction(ResourceHandle handle, uint64_t requested_at_frame, ImageHandle bindless_image) : handle(handle), requested_at_frame(requested_at_frame), bindless_image(bindless_image)
		{}
		ResourceHandle handle;
		uint64_t requested_at_frame;
		BufferHandle bindless_buffer;
		ImageHandle bindless_image;
	};
	uint64_t global_frame_counter = 0;
	std::queue<PendingDestruction> pending_destructions;

	const VulkanMainContext& vmc;
	Storage& storage;

	struct BindlessBuffer
	{
		ResourceHandle resource;
		std::string unique_name;
		uint32_t generation = 0;
	};
	struct BindlessImage
	{
		ResourceHandle resource;
		std::string unique_name;
		uint32_t generation = 0;
	};
	std::vector<BindlessBuffer> buffers_of_bindless;
	std::vector<BindlessImage> images_of_bindless;
	std::vector<uint32_t> free_buffer_indices;
	std::vector<uint32_t> free_image_indices;
	// Map unique names to their handle.
	std::unordered_map<std::string, BufferHandle> buffer_names;
	std::unordered_map<std::string, ImageHandle> image_names;

	uint32_t bindless_image_capacity = max_bindless_images;
	vk::DescriptorSetLayout bindless_set_layout;
	vk::DescriptorPool bindless_pool;
	std::array<vk::DescriptorSet, frames_in_flight> bindless_sets;
	std::array<ResourceHandle, frames_in_flight> address_tables;
	std::array<ResourceHandle, frames_in_flight> buffer_generation_tables;
	std::array<ResourceHandle, frames_in_flight> image_generation_tables;
	// Remember for each frame which indices still need to be updated in begin_frame.
	std::array<std::vector<uint32_t>, frames_in_flight> pending_buffer_writes;
	std::array<std::vector<uint32_t>, frames_in_flight> pending_image_writes;
};
} // namespace vkte
