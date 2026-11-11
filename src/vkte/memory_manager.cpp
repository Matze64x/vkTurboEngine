#include "vkte/memory_manager.hpp"

#include <array>
#include <span>
#include "vkte/global_constants.hpp"
#include "vkte/vkte_log.hpp"
#include "vkte/vulkan_main_context.hpp"

namespace vkte
{
MemoryManager::MemoryManager(const VulkanMainContext& vmc, Storage& storage) : vmc(vmc), storage(storage)
{}

static void log_memory_info(const vk::PhysicalDevice& physical_device)
{
	VKTE_DEBUG("vkte: Memory Info");
	vk::PhysicalDeviceMemoryProperties mp = physical_device.getMemoryProperties();
	for (uint32_t i = 0; i < mp.memoryHeapCount; i++)
	{
		const bool device_local = bool(mp.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal);
		VKTE_DEBUG("  Heap {}: {} MB{}", i, mp.memoryHeaps[i].size / (1024 * 1024), device_local ? " (VRAM)" : "");
	}
	for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
	{
		VKTE_DEBUG("  Type {}: Heap {} | {}", i, mp.memoryTypes[i].heapIndex, vk::to_string(mp.memoryTypes[i].propertyFlags));
	}
}

static ResourceHandle add_table(Storage& storage, const std::string& name, std::span<const std::byte> entries)
{
	Buffer::Settings settings;
	settings.byte_size = entries.size();
	settings.usage_flags = vk::BufferUsageFlagBits::eStorageBuffer;
	settings.location = MemoryLocation::BARFallbackHostVisible;
	settings.queues = QueueFamilyFlags::Graphics | QueueFamilyFlags::Compute | QueueFamilyFlags::Transfer;
	const ResourceHandle table = storage.add_buffer(name, settings);
	storage.get_buffer(table).update_data_bytes(entries.data(), entries.size());
	return table;
}

void MemoryManager::construct()
{
	log_memory_info(vmc.physical_device.get());

	vk::PhysicalDeviceVulkan12Properties vulkan_12_properties;
	vk::PhysicalDeviceProperties2 properties2;
	properties2.pNext = &vulkan_12_properties;
	vmc.physical_device.get().getProperties2(&properties2);
	max_buffer_capacity = uint32_t(properties2.properties.limits.maxStorageBufferRange / sizeof(uint64_t));
	max_image_capacity = std::min({vulkan_12_properties.maxDescriptorSetUpdateAfterBindSampledImages, vulkan_12_properties.maxDescriptorSetUpdateAfterBindSamplers, vulkan_12_properties.maxPerStageDescriptorUpdateAfterBindSampledImages, vulkan_12_properties.maxPerStageDescriptorUpdateAfterBindSamplers, vulkan_12_properties.maxPerStageUpdateAfterBindResources}) - reserved_descriptor_count;
	buffer_capacity = std::min(buffer_capacity, max_buffer_capacity);
	image_capacity = std::min(image_capacity, max_image_capacity);

	std::array<vk::DescriptorSetLayoutBinding, 4> bindings{
		vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll},
		vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll},
		vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll},
		vk::DescriptorSetLayoutBinding{3, vk::DescriptorType::eCombinedImageSampler, max_image_capacity, vk::ShaderStageFlagBits::eAll}};
	std::array<vk::DescriptorBindingFlags, 4> binding_flags{
		vk::DescriptorBindingFlags{},
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

	for (uint32_t i = 0; i < frames_in_flight; i++) create_frame_set(i);
}

// a pending command buffer may still use the frame's old set and tables, so they are destroyed deferred
void MemoryManager::create_frame_set(uint32_t frame_index)
{
	if (bindless_pools[frame_index]) pending_destructions.push(PendingDestruction(bindless_pools[frame_index], global_frame_counter));
	for (const ResourceHandle& table : {buffer_generation_tables[frame_index], address_tables[frame_index], image_generation_tables[frame_index]})
	{
		if (table.valid()) destroy(table);
	}

	std::array<vk::DescriptorPoolSize, 2> pool_sizes{vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, image_capacity}, vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 3}};
	vk::DescriptorPoolCreateInfo dpci;
	dpci.poolSizeCount = pool_sizes.size();
	dpci.pPoolSizes = pool_sizes.data();
	dpci.maxSets = 1;
	dpci.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
	bindless_pools[frame_index] = vmc.logical_device.get().createDescriptorPool(dpci);
	vk::DescriptorSetVariableDescriptorCountAllocateInfo dsvdcai;
	dsvdcai.descriptorSetCount = 1;
	dsvdcai.pDescriptorCounts = &image_capacity;
	vk::DescriptorSetAllocateInfo dsai;
	dsai.descriptorPool = bindless_pools[frame_index];
	dsai.descriptorSetCount = 1;
	dsai.pSetLayouts = &bindless_set_layout;
	dsai.pNext = &dsvdcai;
	bindless_sets[frame_index] = vmc.logical_device.get().allocateDescriptorSets(dsai)[0];

	// the buffer tables with all entries, the images' entries written with their descriptors below
	std::vector<uint32_t> buffer_generations(buffer_capacity, HandleBase::invalid_id);
	std::vector<uint64_t> addresses(buffer_capacity, 0);
	for (uint32_t index = 0; index < buffers_of_bindless.size(); index++)
	{
		if (!buffers_of_bindless[index].resource.valid()) continue;
		buffer_generations[index] = buffers_of_bindless[index].generation;
		addresses[index] = resolve_buffer_address(buffers_of_bindless[index].resource);
	}
	const std::vector<uint32_t> image_generations(image_capacity, HandleBase::invalid_id);
	buffer_generation_tables[frame_index] = add_table(storage, std::format("bindless buffer generation table {} (vkte internal)", frame_index), std::as_bytes(std::span(buffer_generations)));
	address_tables[frame_index] = add_table(storage, std::format("bindless address table {} (vkte internal)", frame_index), std::as_bytes(std::span(addresses)));
	image_generation_tables[frame_index] = add_table(storage, std::format("bindless image generation table {} (vkte internal)", frame_index), std::as_bytes(std::span(image_generations)));
	pending_buffer_writes[frame_index].clear();
	pending_image_writes[frame_index].clear();
	for (uint32_t index = 0; index < images_of_bindless.size(); index++) pending_image_writes[frame_index].push_back(index);
	frame_buffer_capacities[frame_index] = buffer_capacity;
	frame_image_capacities[frame_index] = image_capacity;

	// in the order of their bindings
	const std::array<vk::DescriptorBufferInfo, 3> infos{
		vk::DescriptorBufferInfo(storage.get_buffer(buffer_generation_tables[frame_index]).get(), 0, VK_WHOLE_SIZE),
		vk::DescriptorBufferInfo(storage.get_buffer(address_tables[frame_index]).get(), 0, VK_WHOLE_SIZE),
		vk::DescriptorBufferInfo(storage.get_buffer(image_generation_tables[frame_index]).get(), 0, VK_WHOLE_SIZE)};
	std::array<vk::WriteDescriptorSet, 3> writes;
	for (uint32_t binding = 0; binding < writes.size(); binding++)
	{
		writes[binding].dstSet = bindless_sets[frame_index];
		writes[binding].dstBinding = binding;
		writes[binding].dstArrayElement = 0;
		writes[binding].descriptorType = vk::DescriptorType::eStorageBuffer;
		writes[binding].descriptorCount = 1;
		writes[binding].pBufferInfo = &infos[binding];
	}
	vmc.logical_device.get().updateDescriptorSets(writes, {});
}

void MemoryManager::destruct()
{
	vmc.logical_device.get().waitIdle();
	flush_pending_destructions(true);
	for (const ResourceHandle& address_table : address_tables) storage.destroy(address_table);
	for (const ResourceHandle& image_generation_table : image_generation_tables) storage.destroy(image_generation_table);
	for (const ResourceHandle& buffer_generation_table : buffer_generation_tables) storage.destroy(buffer_generation_table);
	for (const vk::DescriptorPool& pool : bindless_pools) vmc.logical_device.get().destroyDescriptorPool(pool);
	vmc.logical_device.get().destroyDescriptorSetLayout(bindless_set_layout);
}

void MemoryManager::flush_pending_destructions(bool force)
{
	while (!pending_destructions.empty() && (force || global_frame_counter - pending_destructions.front().requested_at_frame >= frames_in_flight))
	{
		const PendingDestruction pd = pending_destructions.front();
		pending_destructions.pop();
		if (pd.bindless_buffer.valid())
		{
			buffers_of_bindless[pd.bindless_buffer.id].generation++;
			free_buffer_indices.push_back(pd.bindless_buffer.id);
		}
		if (pd.bindless_image.valid())
		{
			images_of_bindless[pd.bindless_image.id].generation++;
			free_image_indices.push_back(pd.bindless_image.id);
		}
		if (pd.handle.valid()) storage.destroy(pd.handle);
		if (pd.acceleration_structure) vmc.logical_device.get().destroyAccelerationStructureKHR(pd.acceleration_structure);
		if (pd.descriptor_pool) vmc.logical_device.get().destroyDescriptorPool(pd.descriptor_pool);
		if (pd.query_pool) vmc.logical_device.get().destroyQueryPool(pd.query_pool);
	}
}

void MemoryManager::mark_buffer_dirty(uint32_t index)
{
	for (std::vector<uint32_t>& pending : pending_buffer_writes) pending.push_back(index);
}

void MemoryManager::mark_image_dirty(uint32_t index)
{
	for (std::vector<uint32_t>& pending : pending_image_writes) pending.push_back(index);
}

uint64_t MemoryManager::resolve_buffer_address(const ResourceHandle& resource)
{
	Buffer& buffer = storage.get_buffer(resource);
	if (buffer.pNext && reinterpret_cast<const vk::BaseInStructure*>(buffer.pNext)->sType == vk::StructureType::eWriteDescriptorSetAccelerationStructureKHR)
	{
		const vk::WriteDescriptorSetAccelerationStructureKHR& wdsas = *reinterpret_cast<const vk::WriteDescriptorSetAccelerationStructureKHR*>(buffer.pNext);
		vk::AccelerationStructureDeviceAddressInfoKHR asdai;
		asdai.accelerationStructure = wdsas.pAccelerationStructures[0];
		return vmc.logical_device.get().getAccelerationStructureAddressKHR(&asdai);
	}
	return buffer.get_device_address();
}

BufferHandle MemoryManager::add_bindless_buffer(const std::string& descriptive_name, const Buffer::Settings& settings, const std::string& unique_name)
{
	return register_buffer(storage.add_buffer(descriptive_name, settings), unique_name);
}

ImageHandle MemoryManager::add_bindless_image(const std::string& descriptive_name, const Image::Settings& settings, const std::string& unique_name)
{
	return register_image(storage.add_image(descriptive_name, settings), unique_name);
}

void MemoryManager::destroy(const BufferHandle& handle)
{
	VKTE_ASSERT(handle.id < buffers_of_bindless.size() && buffers_of_bindless[handle.id].resource.valid() && handle.generation == buffers_of_bindless[handle.id].generation,
		"vkte: Trying to destroy an invalid, already destroyed, or stale bindless buffer handle");
	BindlessBuffer& buffer = buffers_of_bindless[handle.id];
	pending_destructions.push(PendingDestruction(buffer.resource, global_frame_counter, handle));
	if (!buffer.unique_name.empty()) buffer_names.erase(buffer.unique_name);
	buffer.resource = ResourceHandle{};
	buffer.unique_name.clear();
	mark_buffer_dirty(handle.id);
}

void MemoryManager::destroy(const ImageHandle& handle)
{
	VKTE_ASSERT(handle.id < images_of_bindless.size() && images_of_bindless[handle.id].resource.valid() && handle.generation == images_of_bindless[handle.id].generation,
		"vkte: Trying to destroy an invalid, already destroyed, or stale bindless image handle");
	BindlessImage& image = images_of_bindless[handle.id];
	pending_destructions.push(PendingDestruction(image.resource, global_frame_counter, handle));
	if (!image.unique_name.empty()) image_names.erase(image.unique_name);
	image.resource = ResourceHandle{};
	image.unique_name.clear();
	mark_image_dirty(handle.id);
}

void MemoryManager::destroy(const ResourceHandle& storage_resource)
{
	pending_destructions.push(PendingDestruction(storage_resource, global_frame_counter, BufferHandle()));
}

void MemoryManager::destroy(vk::AccelerationStructureKHR acceleration_structure)
{
	pending_destructions.push(PendingDestruction(acceleration_structure, global_frame_counter));
}

void MemoryManager::destroy(vk::QueryPool query_pool)
{
	pending_destructions.push(PendingDestruction(query_pool, global_frame_counter));
}

Buffer& MemoryManager::get_buffer(const BufferHandle& handle)
{
	VKTE_ASSERT(handle.id < buffers_of_bindless.size() && buffers_of_bindless[handle.id].resource.valid() && handle.generation == buffers_of_bindless[handle.id].generation,
		"vkte: Trying to get an invalid, already destroyed, or stale bindless buffer handle");
	return storage.get_buffer(buffers_of_bindless[handle.id].resource);
}

Image& MemoryManager::get_image(const ImageHandle& handle)
{
	VKTE_ASSERT(handle.id < images_of_bindless.size() && images_of_bindless[handle.id].resource.valid() && handle.generation == images_of_bindless[handle.id].generation,
		"vkte: Trying to get an invalid, already destroyed, or stale bindless image handle");
	return storage.get_image(images_of_bindless[handle.id].resource);
}

BufferHandle MemoryManager::get_bindless_buffer(const std::string& unique_name)
{
	VKTE_ASSERT(buffer_names.contains(unique_name), "vkte: Trying to get an invalid or already destroyed buffer handle");
	return buffer_names.at(unique_name);
}

ImageHandle MemoryManager::get_bindless_image(const std::string& unique_name)
{
	VKTE_ASSERT(image_names.contains(unique_name), "vkte: Trying to get an invalid or already destroyed image handle");
	return image_names.at(unique_name);
}

BufferHandle MemoryManager::register_buffer(const ResourceHandle& buffer_handle, const std::string& unique_name)
{
	VKTE_ASSERT(unique_name.empty() || !buffer_names.contains(unique_name), std::format("vkte: Duplicate bindless buffer name: {}", unique_name));
	uint32_t index;
	if (!free_buffer_indices.empty())
	{
		index = free_buffer_indices.back();
		free_buffer_indices.pop_back();
	}
	else
	{
		VKTE_ASSERT(buffers_of_bindless.size() < max_buffer_capacity, std::format("vkte: The device allows at most {} bindless buffers", max_buffer_capacity));
		if (buffers_of_bindless.size() == buffer_capacity)
		{
			buffer_capacity = std::min(buffer_capacity * 2, max_buffer_capacity);
			VKTE_DEBUG("vkte: Growing the bindless buffer tables to {} slots", buffer_capacity);
		}
		buffers_of_bindless.emplace_back();
		index = uint32_t(buffers_of_bindless.size() - 1);
	}
	BindlessBuffer& buffer = buffers_of_bindless[index];
	buffer.resource = buffer_handle;
	buffer.unique_name = unique_name;
	mark_buffer_dirty(index);
	const BufferHandle handle{index, buffer.generation};
	if (!unique_name.empty()) buffer_names.emplace(unique_name, handle);
	return handle;
}

ImageHandle MemoryManager::register_image(const ResourceHandle& image_handle, const std::string& unique_name)
{
	VKTE_ASSERT(unique_name.empty() || !image_names.contains(unique_name), std::format("vkte: Duplicate bindless image name: {}", unique_name));
	uint32_t index;
	if (!free_image_indices.empty())
	{
		index = free_image_indices.back();
		free_image_indices.pop_back();
	}
	else
	{
		VKTE_ASSERT(images_of_bindless.size() < max_image_capacity, std::format("vkte: The device allows at most {} bindless images", max_image_capacity));
		if (images_of_bindless.size() == image_capacity)
		{
			image_capacity = std::min(image_capacity * 2, max_image_capacity);
			VKTE_DEBUG("vkte: Growing the bindless image set to {} slots", image_capacity);
		}
		images_of_bindless.emplace_back();
		index = uint32_t(images_of_bindless.size() - 1);
	}
	BindlessImage& image = images_of_bindless[index];
	image.resource = image_handle;
	image.unique_name = unique_name;
	mark_image_dirty(index);
	const ImageHandle handle{index, image.generation};
	if (!unique_name.empty()) image_names.emplace(unique_name, handle);
	return handle;
}

void MemoryManager::unregister_buffer(const BufferHandle& handle)
{
	VKTE_ASSERT(handle.id < buffers_of_bindless.size() && buffers_of_bindless[handle.id].resource.valid() && handle.generation == buffers_of_bindless[handle.id].generation,
		"vkte: Trying to unregister an invalid, already unregistered, or stale buffer handle");
	BindlessBuffer& buffer = buffers_of_bindless[handle.id];
	pending_destructions.push(PendingDestruction(ResourceHandle{}, global_frame_counter, handle));
	if (!buffer.unique_name.empty()) buffer_names.erase(buffer.unique_name);
	buffer.resource = ResourceHandle{};
	buffer.unique_name.clear();
	mark_buffer_dirty(handle.id);
}

void MemoryManager::unregister_image(const ImageHandle& handle)
{
	VKTE_ASSERT(handle.id < images_of_bindless.size() && images_of_bindless[handle.id].resource.valid() && handle.generation == images_of_bindless[handle.id].generation,
		"vkte: Trying to unregister an invalid, already unregistered, or stale image handle");
	BindlessImage& image = images_of_bindless[handle.id];
	pending_destructions.push(PendingDestruction(ResourceHandle{}, global_frame_counter, handle));
	if (!image.unique_name.empty()) image_names.erase(image.unique_name);
	image.resource = ResourceHandle{};
	image.unique_name.clear();
	mark_image_dirty(handle.id);
}

void MemoryManager::begin_frame()
{
	global_frame_counter++;
	flush_pending_destructions(false);
}

void MemoryManager::write_descriptors(uint32_t frame_index)
{
	if (frame_buffer_capacities[frame_index] != buffer_capacity || frame_image_capacities[frame_index] != image_capacity) create_frame_set(frame_index);
	std::vector<uint32_t>& dirty_buffers = pending_buffer_writes[frame_index];
	if (!dirty_buffers.empty())
	{
		Buffer& address_table = storage.get_buffer(address_tables[frame_index]);
		Buffer& generation_table = storage.get_buffer(buffer_generation_tables[frame_index]);
		for (uint32_t index : dirty_buffers)
		{
			const bool valid = buffers_of_bindless[index].resource.valid();
			const uint64_t address = valid ? resolve_buffer_address(buffers_of_bindless[index].resource) : 0;
			address_table.update_data_bytes(&address, sizeof(uint64_t), index * sizeof(uint64_t));
			const uint32_t generation = valid ? buffers_of_bindless[index].generation : HandleBase::invalid_id;
			generation_table.update_data_bytes(&generation, sizeof(uint32_t), index * sizeof(uint32_t));
		}
		dirty_buffers.clear();
	}

	std::vector<uint32_t>& dirty_images = pending_image_writes[frame_index];
	if (!dirty_images.empty())
	{
		Buffer& generation_table = storage.get_buffer(image_generation_tables[frame_index]);
		std::vector<vk::WriteDescriptorSet> writes;
		std::vector<vk::DescriptorImageInfo> image_infos;
		writes.reserve(dirty_images.size());
		image_infos.reserve(dirty_images.size());
		for (uint32_t index : dirty_images)
		{
			const bool valid = images_of_bindless[index].resource.valid();
			const uint32_t generation = valid ? images_of_bindless[index].generation : HandleBase::invalid_id;
			generation_table.update_data_bytes(&generation, sizeof(uint32_t), index * sizeof(uint32_t));
			if (!valid) continue;

			Image& image = storage.get_image(images_of_bindless[index].resource);
			image_infos.push_back(vk::DescriptorImageInfo(image.get_sampler(), image.get_view(), image.get_layout()));

			vk::WriteDescriptorSet wds;
			wds.dstSet = bindless_sets[frame_index];
			wds.dstBinding = 3;
			wds.dstArrayElement = index;
			wds.descriptorType = vk::DescriptorType::eCombinedImageSampler;
			wds.descriptorCount = 1;
			wds.pImageInfo = &image_infos.back();
			writes.push_back(wds);
		}
		if (!writes.empty()) vmc.logical_device.get().updateDescriptorSets(writes, {});
		dirty_images.clear();
	}
}

void MemoryManager::bind_bindless_set(vk::CommandBuffer cb, vk::PipelineBindPoint bind_point, const vk::PipelineLayout& pipeline_layout, uint32_t frame_index) const
{
	cb.bindDescriptorSets(bind_point, pipeline_layout, 0, {bindless_sets[frame_index]}, {});
}
} // namespace vkte
