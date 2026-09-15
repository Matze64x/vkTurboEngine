#pragma once

#include <array>
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
	ImageHandle register_texture(const ResourceHandle& image);
	void unregister_texture(const ImageHandle& handle);
	BufferHandle register_buffer(const ResourceHandle& buffer);
	void unregister_buffer(const BufferHandle& handle);

private:
	static constexpr uint32_t max_bindless_textures = 65536;
	static constexpr uint32_t max_bindless_buffers = 65536;

	friend class Engine;
	MemoryManager(const VulkanMainContext& vmc, Storage& storage);
	void construct();
	void destruct();
	const vk::DescriptorSetLayout& get_bindless_set_layout() const { return bindless_set_layout; }
	void begin_frame(uint32_t frame_index);
	void bind_bindless_set(vk::CommandBuffer cb, vk::PipelineBindPoint bind_point, const vk::PipelineLayout& pipeline_layout, uint32_t frame_index) const;

	struct IndexAllocator
	{
		uint32_t allocate(uint32_t capacity);
		void free(uint32_t index);

		uint32_t next = 0;
		std::vector<uint32_t> free_indices;
	};
	void mark_texture_dirty(uint32_t index);
	void mark_buffer_dirty(uint32_t index);

	const VulkanMainContext& vmc;
	Storage& storage;

	uint32_t bindless_texture_capacity = max_bindless_textures;
	vk::DescriptorSetLayout bindless_set_layout;
	vk::DescriptorPool bindless_pool;
	std::array<vk::DescriptorSet, frames_in_flight> bindless_sets;
	std::array<ResourceHandle, frames_in_flight> address_tables;
	std::array<ResourceHandle, frames_in_flight> texture_validity_tables;
	// CPU side copy what the buffer table and the texture descriptors should contain.
	std::vector<uint64_t> buffer_addresses;

	struct TextureEntry
	{
		uint32_t valid = 0;
		vk::DescriptorImageInfo dii;
	};
	std::vector<TextureEntry> texture_infos;
	// Remember for each frame which indices still need to be updated in begin_frame.
	std::array<std::vector<uint32_t>, frames_in_flight> pending_buffer_writes;
	std::array<std::vector<uint32_t>, frames_in_flight> pending_texture_writes;
	IndexAllocator texture_indices;
	IndexAllocator buffer_indices;
};
} // namespace vkte
