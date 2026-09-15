#include "vkte/memory_manager.hpp"

#include <algorithm>
#include <array>
#include "vkte/global_constants.hpp"
#include "vkte/vkte_log.hpp"

namespace vkte
{
MemoryManager::MemoryManager(const VulkanMainContext& vmc, Storage& storage) : vmc(vmc), storage(storage)
{}

uint32_t MemoryManager::IndexAllocator::allocate(uint32_t capacity)
{
	if (!free_indices.empty())
	{
		const uint32_t index = free_indices.back();
		free_indices.pop_back();
		return index;
	}
	VKTE_ASSERT(next < capacity, "vkte: Bindless index allocator exhausted its capacity!");
	return next++;
}

void MemoryManager::IndexAllocator::free(uint32_t index)
{
	free_indices.push_back(index);
}

void MemoryManager::construct()
{
	vk::PhysicalDeviceVulkan12Properties vulkan_12_properties;
	vk::PhysicalDeviceProperties2 properties2;
	properties2.pNext = &vulkan_12_properties;
	vmc.physical_device.get().getProperties2(&properties2);
	bindless_texture_capacity = std::min({max_bindless_textures, vulkan_12_properties.maxDescriptorSetUpdateAfterBindSampledImages, vulkan_12_properties.maxDescriptorSetUpdateAfterBindSamplers});
	buffer_addresses.resize(max_bindless_buffers, 0);
	texture_infos.resize(bindless_texture_capacity);

	std::array<vk::DescriptorSetLayoutBinding, 3> bindings{
		vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll},
		vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll},
		vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eCombinedImageSampler, bindless_texture_capacity, vk::ShaderStageFlagBits::eAll}};
	std::array<vk::DescriptorBindingFlags, 3> binding_flags{
		vk::DescriptorBindingFlags{},
		vk::DescriptorBindingFlags{},
		vk::DescriptorBindingFlagBits::eUpdateAfterBind | vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eVariableDescriptorCount};

	vk::DescriptorSetLayoutBindingFlagsCreateInfo dslbfci;
	dslbfci.bindingCount = binding_flags.size();
	dslbfci.pBindingFlags = binding_flags.data();

	vk::DescriptorSetLayoutCreateInfo dslci;
	dslci.bindingCount = bindings.size();
	dslci.pBindings = bindings.data();
	dslci.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
	dslci.pNext = &dslbfci;
	bindless_set_layout = vmc.logical_device.get().createDescriptorSetLayout(dslci);

	std::array<vk::DescriptorPoolSize, 2> pool_sizes{
		vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, bindless_texture_capacity * frames_in_flight},
		vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 2 * frames_in_flight}};
	vk::DescriptorPoolCreateInfo dpci;
	dpci.poolSizeCount = pool_sizes.size();
	dpci.pPoolSizes = pool_sizes.data();
	dpci.maxSets = frames_in_flight;
	dpci.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
	bindless_pool = vmc.logical_device.get().createDescriptorPool(dpci);

	const uint32_t variable_count = bindless_texture_capacity;
	std::vector<uint32_t> variable_counts(frames_in_flight, variable_count);
	vk::DescriptorSetVariableDescriptorCountAllocateInfo dsvdcai;
	dsvdcai.descriptorSetCount = frames_in_flight;
	dsvdcai.pDescriptorCounts = variable_counts.data();

	std::vector<vk::DescriptorSetLayout> set_layouts(frames_in_flight, bindless_set_layout);
	vk::DescriptorSetAllocateInfo dsai;
	dsai.descriptorPool = bindless_pool;
	dsai.descriptorSetCount = frames_in_flight;
	dsai.pSetLayouts = set_layouts.data();
	dsai.pNext = &dsvdcai;
	const std::vector<vk::DescriptorSet> allocated_sets = vmc.logical_device.get().allocateDescriptorSets(dsai);

	for (uint32_t i = 0; i < frames_in_flight; i++)
	{
		bindless_sets[i] = allocated_sets[i];
		address_tables[i] = storage.add_buffer(std::format("bindless address table {} (vkte internal)", i), uint64_t(max_bindless_buffers) * sizeof(uint64_t), vk::BufferUsageFlagBits::eStorageBuffer, false, QueueFamilyFlags::Graphics | QueueFamilyFlags::Compute | QueueFamilyFlags::Transfer);
		storage.get_buffer(address_tables[i]).update_data_bytes(0, uint64_t(max_bindless_buffers) * sizeof(uint64_t));

		texture_validity_tables[i] = storage.add_buffer(std::format("bindless texture validity table {} (vkte internal)", i), uint64_t(bindless_texture_capacity) * sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, false, QueueFamilyFlags::Graphics | QueueFamilyFlags::Compute | QueueFamilyFlags::Transfer);
		storage.get_buffer(texture_validity_tables[i]).update_data_bytes(0, uint64_t(bindless_texture_capacity) * sizeof(uint32_t));

		vk::DescriptorBufferInfo address_table_dbi(storage.get_buffer(address_tables[i]).get(), 0, VK_WHOLE_SIZE);
		vk::DescriptorBufferInfo texture_valid_dbi(storage.get_buffer(texture_validity_tables[i]).get(), 0, VK_WHOLE_SIZE);
		std::array<vk::WriteDescriptorSet, 2> writes;
		writes[0].dstSet = bindless_sets[i];
		writes[0].dstBinding = 0;
		writes[0].dstArrayElement = 0;
		writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
		writes[0].descriptorCount = 1;
		writes[0].pBufferInfo = &address_table_dbi;
		writes[1].dstSet = bindless_sets[i];
		writes[1].dstBinding = 1;
		writes[1].dstArrayElement = 0;
		writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
		writes[1].descriptorCount = 1;
		writes[1].pBufferInfo = &texture_valid_dbi;
		vmc.logical_device.get().updateDescriptorSets(writes, {});
	}
}

void MemoryManager::destruct()
{
	for (const ResourceHandle& address_table : address_tables) storage.destroy(address_table);
	for (const ResourceHandle& texture_validity_table : texture_validity_tables) storage.destroy(texture_validity_table);
	vmc.logical_device.get().destroyDescriptorPool(bindless_pool);
	vmc.logical_device.get().destroyDescriptorSetLayout(bindless_set_layout);
}

void MemoryManager::mark_texture_dirty(uint32_t index)
{
	for (std::vector<uint32_t>& pending : pending_texture_writes) pending.push_back(index);
}

void MemoryManager::mark_buffer_dirty(uint32_t index)
{
	for (std::vector<uint32_t>& pending : pending_buffer_writes) pending.push_back(index);
}

ImageHandle MemoryManager::register_texture(const ResourceHandle& image_handle)
{
	const uint32_t index = texture_indices.allocate(bindless_texture_capacity);
	Image& image = storage.get_image(image_handle);
	texture_infos[index].dii = vk::DescriptorImageInfo(image.get_sampler(), image.get_view(), image.get_layout());
	texture_infos[index].valid = 1;
	mark_texture_dirty(index);
	return ImageHandle{index};
}

void MemoryManager::unregister_texture(const ImageHandle& image_handle)
{
	texture_infos[image_handle.id].valid = 0;
	mark_texture_dirty(image_handle.id);
	texture_indices.free(image_handle.id);
}

BufferHandle MemoryManager::register_buffer(const ResourceHandle& buffer_handle)
{
	const uint32_t index = buffer_indices.allocate(max_bindless_buffers);
	buffer_addresses[index] = storage.get_buffer(buffer_handle).get_device_address();
	mark_buffer_dirty(index);
	return BufferHandle{index};
}

void MemoryManager::unregister_buffer(const BufferHandle& buffer_handle)
{
	buffer_addresses[buffer_handle.id] = 0;
	mark_buffer_dirty(buffer_handle.id);
	buffer_indices.free(buffer_handle.id);
}

void MemoryManager::begin_frame(uint32_t frame_index)
{
	std::vector<uint32_t>& dirty_buffers = pending_buffer_writes[frame_index];
	if (!dirty_buffers.empty())
	{
		Buffer& address_table = storage.get_buffer(address_tables[frame_index]);
		for (uint32_t index : dirty_buffers) address_table.update_data_bytes(&buffer_addresses[index], sizeof(uint64_t), index * sizeof(uint64_t));
		dirty_buffers.clear();
	}

	std::vector<uint32_t>& dirty_textures = pending_texture_writes[frame_index];
	if (!dirty_textures.empty())
	{
		Buffer& validity_table = storage.get_buffer(texture_validity_tables[frame_index]);
		std::vector<vk::WriteDescriptorSet> writes;
		writes.reserve(dirty_textures.size());
		for (uint32_t index : dirty_textures)
		{
			validity_table.update_data_bytes(&texture_infos[index].valid, sizeof(uint32_t), index * sizeof(uint32_t));

			vk::WriteDescriptorSet wds;
			wds.dstSet = bindless_sets[frame_index];
			wds.dstBinding = 2;
			wds.dstArrayElement = index;
			wds.descriptorType = vk::DescriptorType::eCombinedImageSampler;
			wds.descriptorCount = 1;
			wds.pImageInfo = &texture_infos[index].dii;
			writes.push_back(wds);
		}
		vmc.logical_device.get().updateDescriptorSets(writes, {});
		dirty_textures.clear();
	}
}

void MemoryManager::bind_bindless_set(vk::CommandBuffer cb, vk::PipelineBindPoint bind_point, const vk::PipelineLayout& pipeline_layout, uint32_t frame_index) const
{
	cb.bindDescriptorSets(bind_point, pipeline_layout, 0, {bindless_sets[frame_index]}, {});
}
} // namespace vkte
