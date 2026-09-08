#include "vkte/storage.hpp"

#include "vkte/vkte_log.hpp"

namespace vkte
{
Storage::Storage(const VulkanMainContext& vmc, VulkanCommandContext& vcc) : vmc(vmc), vcc(vcc)
{}

std::string Storage::get_memory_info()
{
	std::string info_string = "";
	vk::PhysicalDeviceMemoryProperties memory_properties = vmc.physical_device.get().getMemoryProperties();
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

void Storage::destroy_buffer(uint32_t idx)
{
	if (buffers.at(idx)->buffer.has_value())
	{
		VmaAllocationInfo alloc_info = buffers.at(idx)->buffer.value().get_allocation_info();
		VKTE_DEBUG("vkte: Destroying buffer \"{}\", Size: {}, Type: {}", buffers.at(idx)->name, alloc_info.size, alloc_info.memoryType);
		buffers.at(idx)->buffer.value().destruct();
		buffers.at(idx)->buffer.reset();
	}
	else
	{
		VKTE_ERROR("vkte: Trying to destroy already destroyed buffer!");
	}
}

void Storage::destroy_image(uint32_t idx)
{
	if (images.at(idx)->image.has_value())
	{
		VmaAllocationInfo alloc_info = images.at(idx)->image.value().get_allocation_info();
		VKTE_DEBUG("vkte: Destroying image \"{}\", Size: {}, Type: {}", images.at(idx)->name, alloc_info.size, alloc_info.memoryType);
		images.at(idx)->image.value().destruct();
		images.at(idx)->image.reset();
	}
	else
	{
		VKTE_ERROR("vkte: Trying to destroy already destroyed image!");
	}
}

void Storage::destroy_buffer(const std::string& name)
{
	destroy_buffer(buffer_names.at(name));
}

void Storage::destroy_image(const std::string& name)
{
	destroy_image(image_names.at(name));
}

void Storage::clear()
{
	for (const std::pair<std::string, uint32_t>& buffer : buffer_names)
	{
		if (buffers[buffer.second]->buffer.has_value())
		{
			VKTE_WARN("vkte: Buffer \"{}\" not destroyed! Cleaning up...", buffer.first);
			buffers[buffer.second]->buffer.value().destruct();
		}
	}
	buffers.clear();
	buffer_names.clear();
	for (const std::pair<std::string, uint32_t>& image : image_names)
	{
		if (images[image.second]->image.has_value())
		{
			VKTE_WARN("vkte: Image \"{}\" not destroyed! Cleaning up...", image.first);
			images[image.second]->image.value().destruct();
		}
	}
	images.clear();
	image_names.clear();
}

Buffer& Storage::get_buffer(uint32_t idx)
{
	if (!buffers.at(idx)->buffer.has_value()) VKTE_THROW("vkte: Trying to get already destroyed buffer!");
	return buffers.at(idx)->buffer.value();
}

Image& Storage::get_image(uint32_t idx)
{
	if (!images.at(idx)->image.has_value()) VKTE_THROW("vkte: Trying to get already destroyed image!");
	return images.at(idx)->image.value();
}

uint32_t Storage::get_buffer_index(const std::string& name) const
{
	if (!buffer_names.contains(name)) VKTE_THROW("vkte: Failed to find buffer with name: " + name);
	return buffer_names.at(name);
}

uint32_t Storage::get_image_index(const std::string& name) const
{
	if (!image_names.contains(name)) VKTE_THROW("vkte: Failed to find image with name: " + name);
	return image_names.at(name);
}

Buffer& Storage::get_buffer_by_name(const std::string& name)
{
	return get_buffer(get_buffer_index(name));
}

Image& Storage::get_image_by_name(const std::string& name)
{
	return get_image(get_image_index(name));
}
} // namespace vkte
