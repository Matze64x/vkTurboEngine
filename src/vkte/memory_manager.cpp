#include "vkte/memory_manager.hpp"

#include <algorithm>
#include <array>
#include "vkte/global_constants.hpp"
#include "vkte/vkte_log.hpp"
#include "vkte/vulkan_main_context.hpp"

namespace vkte
{
MemoryManager::MemoryManager(const VulkanMainContext& vmc, Storage& storage) : vmc(vmc), storage(storage)
{}

static std::string get_memory_info(const vk::PhysicalDevice& physical_device)
{
	std::string info_string = "";
	vk::PhysicalDeviceMemoryProperties memory_properties = physical_device.getMemoryProperties();
	info_string.append(std::format("Memory Heaps: {}\n", memory_properties.memoryHeapCount));
	for (uint32_t i = 0; i < memory_properties.memoryHeapCount; i++) {
		info_string.append(std::format("Heap {}: {} MB", i, (memory_properties.memoryHeaps[i].size / (1024 * 1024))));
		if (memory_properties.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal) {
			info_string.append(" (Device Local - VRAM)");
		}
		info_string.append("\n");
	}

	info_string.append(std::format("Memory Types: {}\n", memory_properties.memoryTypeCount));
	for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
		info_string.append(std::format("Type {}: Heap {} | ", i, memory_properties.memoryTypes[i].heapIndex));
		if (memory_properties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)
			info_string.append("DEVICE_LOCAL ");
		if (memory_properties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)
			info_string.append("HOST_VISIBLE ");
		if (memory_properties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostCoherent)
			info_string.append("HOST_COHERENT ");
		if (memory_properties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostCached)
			info_string.append("HOST_CACHED ");
		if (memory_properties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eLazilyAllocated)
			info_string.append("LAZILY_ALLOCATED ");
		info_string.append("\n");
	}
	return info_string;
}

void MemoryManager::construct()
{
	VKTE_DEBUG("vkte: {}", get_memory_info(vmc.physical_device.get()));

	vk::PhysicalDeviceVulkan12Properties vulkan_12_properties;
	vk::PhysicalDeviceProperties2 properties2;
	properties2.pNext = &vulkan_12_properties;
	vmc.physical_device.get().getProperties2(&properties2);
	bindless_image_capacity = std::min({max_bindless_images, vulkan_12_properties.maxDescriptorSetUpdateAfterBindSampledImages, vulkan_12_properties.maxDescriptorSetUpdateAfterBindSamplers});

	std::array<vk::DescriptorSetLayoutBinding, 4> bindings{
		vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll},
		vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll},
		vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll},
		vk::DescriptorSetLayoutBinding{3, vk::DescriptorType::eCombinedImageSampler, bindless_image_capacity, vk::ShaderStageFlagBits::eAll}};
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

	std::array<vk::DescriptorPoolSize, 2> pool_sizes{
		vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, bindless_image_capacity * frames_in_flight},
		vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 3 * frames_in_flight}};
	vk::DescriptorPoolCreateInfo dpci;
	dpci.poolSizeCount = pool_sizes.size();
	dpci.pPoolSizes = pool_sizes.data();
	dpci.maxSets = frames_in_flight;
	dpci.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
	bindless_pool = vmc.logical_device.get().createDescriptorPool(dpci);

	const uint32_t variable_count = bindless_image_capacity;
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

		Buffer::Settings buffer_generation_table_settings;
		buffer_generation_table_settings.byte_size = uint64_t(max_bindless_buffers) * sizeof(uint32_t);
		buffer_generation_table_settings.usage_flags = vk::BufferUsageFlagBits::eStorageBuffer;
		buffer_generation_table_settings.location = MemoryLocation::BARFallbackHostVisible;
		buffer_generation_table_settings.queues = QueueFamilyFlags::Graphics | QueueFamilyFlags::Compute | QueueFamilyFlags::Transfer;
		buffer_generation_tables[i] = storage.add_buffer(std::format("bindless buffer generation table {} (vkte internal)", i), buffer_generation_table_settings);
		storage.get_buffer(buffer_generation_tables[i]).fill_bytes(0xFF, buffer_generation_table_settings.byte_size);

		Buffer::Settings address_table_settings;
		address_table_settings.byte_size = uint64_t(max_bindless_buffers) * sizeof(uint64_t);
		address_table_settings.usage_flags = vk::BufferUsageFlagBits::eStorageBuffer;
		address_table_settings.location = MemoryLocation::BARFallbackHostVisible;
		address_table_settings.queues = QueueFamilyFlags::Graphics | QueueFamilyFlags::Compute | QueueFamilyFlags::Transfer;
		address_tables[i] = storage.add_buffer(std::format("bindless address table {} (vkte internal)", i), address_table_settings);
		storage.get_buffer(address_tables[i]).fill_bytes(0, address_table_settings.byte_size);

		Buffer::Settings image_generation_table_settings;
		image_generation_table_settings.byte_size = uint64_t(bindless_image_capacity) * sizeof(uint32_t);
		image_generation_table_settings.usage_flags = vk::BufferUsageFlagBits::eStorageBuffer;
		image_generation_table_settings.location = MemoryLocation::BARFallbackHostVisible;
		image_generation_table_settings.queues = QueueFamilyFlags::Graphics | QueueFamilyFlags::Compute | QueueFamilyFlags::Transfer;
		image_generation_tables[i] = storage.add_buffer(std::format("bindless image generation table {} (vkte internal)", i), image_generation_table_settings);
		storage.get_buffer(image_generation_tables[i]).fill_bytes(0xFF, image_generation_table_settings.byte_size);

		vk::DescriptorBufferInfo address_table_dbi(storage.get_buffer(address_tables[i]).get(), 0, VK_WHOLE_SIZE);
		vk::DescriptorBufferInfo image_generation_dbi(storage.get_buffer(image_generation_tables[i]).get(), 0, VK_WHOLE_SIZE);
		vk::DescriptorBufferInfo buffer_generation_dbi(storage.get_buffer(buffer_generation_tables[i]).get(), 0, VK_WHOLE_SIZE);
		std::array<vk::WriteDescriptorSet, 3> writes;
		writes[0].dstSet = bindless_sets[i];
		writes[0].dstBinding = 0;
		writes[0].dstArrayElement = 0;
		writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
		writes[0].descriptorCount = 1;
		writes[0].pBufferInfo = &buffer_generation_dbi;
		writes[1].dstSet = bindless_sets[i];
		writes[1].dstBinding = 1;
		writes[1].dstArrayElement = 0;
		writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
		writes[1].descriptorCount = 1;
		writes[1].pBufferInfo = &address_table_dbi;
		writes[2].dstSet = bindless_sets[i];
		writes[2].dstBinding = 2;
		writes[2].dstArrayElement = 0;
		writes[2].descriptorType = vk::DescriptorType::eStorageBuffer;
		writes[2].descriptorCount = 1;
		writes[2].pBufferInfo = &image_generation_dbi;
		vmc.logical_device.get().updateDescriptorSets(writes, {});
	}
}

void MemoryManager::destruct()
{
	vmc.logical_device.get().waitIdle();
	flush_pending_destructions(true);
	for (const ResourceHandle& address_table : address_tables) storage.destroy(address_table);
	for (const ResourceHandle& image_generation_table : image_generation_tables) storage.destroy(image_generation_table);
	for (const ResourceHandle& buffer_generation_table : buffer_generation_tables) storage.destroy(buffer_generation_table);
	vmc.logical_device.get().destroyDescriptorPool(bindless_pool);
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
		storage.destroy(pd.handle);
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
		VKTE_ASSERT(buffers_of_bindless.size() < max_bindless_buffers, "vkte: Bindless buffer table exhausted its capacity!");
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
		VKTE_ASSERT(images_of_bindless.size() < max_bindless_images, "vkte: Bindless image table exhausted its capacity!");
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
	if (!buffer.unique_name.empty()) buffer_names.erase(buffer.unique_name);
	buffer.resource = ResourceHandle{};
	buffer.unique_name.clear();
	buffer.generation++;
	mark_buffer_dirty(handle.id);
	free_buffer_indices.push_back(handle.id);
}

void MemoryManager::unregister_image(const ImageHandle& handle)
{
	VKTE_ASSERT(handle.id < images_of_bindless.size() && images_of_bindless[handle.id].resource.valid() && handle.generation == images_of_bindless[handle.id].generation,
		"vkte: Trying to unregister an invalid, already unregistered, or stale image handle");
	BindlessImage& image = images_of_bindless[handle.id];
	if (!image.unique_name.empty()) image_names.erase(image.unique_name);
	image.resource = ResourceHandle{};
	image.unique_name.clear();
	image.generation++;
	mark_image_dirty(handle.id);
	free_image_indices.push_back(handle.id);
}

void MemoryManager::begin_frame(uint32_t frame_index)
{
	global_frame_counter++;
	flush_pending_destructions(false);

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
