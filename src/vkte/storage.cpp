#include "vkte/storage.hpp"

#include "vkte/command.hpp"
#include "vkte/vkte_log.hpp"
#include "vkte/vulkan_main_context.hpp"

namespace vkte
{
Storage::Storage(const VulkanMainContext& vmc, Command& command) : vmc(vmc), command(command)
{}

ResourceHandle Storage::add_buffer(const std::string& name, const Buffer::Settings& settings)
{
	uint32_t idx;
	if (!free_buffer_slots.empty())
	{
		idx = free_buffer_slots.back();
		free_buffer_slots.pop_back();
		buffers.at(idx)->name = name;
		buffers.at(idx)->buffer.emplace(vmc, command, settings);
	}
	else
	{
		buffers.push_back(std::make_unique<BufferElement>(name, vmc, command, settings));
		idx = uint32_t(buffers.size() - 1);
	}
	const vk::Buffer& b = buffers.at(idx)->buffer.value().get();
	vk::DebugUtilsObjectNameInfoEXT duoni(b.objectType, uint64_t(static_cast<vk::Buffer::CType>(b)), name.c_str());
	vmc.logical_device.get().setDebugUtilsObjectNameEXT(duoni);
	VmaAllocationInfo alloc_info = buffers.at(idx)->buffer.value().get_allocation_info();
	VKTE_DEBUG("vkte: Creating buffer \"{}\", Size: {}, Type: {}", name, alloc_info.size, alloc_info.memoryType);
	return ResourceHandle(idx, buffers.at(idx)->generation, false);
}

ResourceHandle Storage::add_image(const std::string& name, const Image::Settings& settings)
{
	uint32_t idx;
	if (!free_image_slots.empty())
	{
		idx = free_image_slots.back();
		free_image_slots.pop_back();
		images.at(idx)->name = name;
		images.at(idx)->image.emplace(vmc, command, settings);
	}
	else
	{
		images.push_back(std::make_unique<ImageElement>(name, vmc, command, settings));
		idx = uint32_t(images.size() - 1);
	}
	const vk::Image& i = images.at(idx)->image.value().get_image();
	vk::DebugUtilsObjectNameInfoEXT duoni(i.objectType, uint64_t(static_cast<vk::Image::CType>(i)), name.c_str());
	vmc.logical_device.get().setDebugUtilsObjectNameEXT(duoni);
	VmaAllocationInfo alloc_info = images.at(idx)->image.value().get_allocation_info();
	VKTE_DEBUG("vkte: Creating image \"{}\", Size: {}, Type: {}", name, alloc_info.size, alloc_info.memoryType);
	return ResourceHandle(idx, images.at(idx)->generation, true);
}

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

void Storage::destroy(const ResourceHandle& handle)
{
	if (handle.is_image)
	{
		ImageElement& element = *images.at(handle.id);
		VKTE_ASSERT(handle.generation == element.generation, std::format("vkte: Trying to destroy image \"{}\" through a stale handle!", element.name));
		if (element.image.has_value())
		{
			VmaAllocationInfo alloc_info = element.image.value().get_allocation_info();
			VKTE_DEBUG("vkte: Destroying image \"{}\", Size: {}, Type: {}", element.name, alloc_info.size, alloc_info.memoryType);
			element.image.reset();
			element.generation++;
			free_image_slots.push_back(handle.id);
		}
		else
		{
			VKTE_ERROR("vkte: Trying to destroy already destroyed image!");
		}
	}
	else
	{
		BufferElement& element = *buffers.at(handle.id);
		VKTE_ASSERT(handle.generation == element.generation, std::format("vkte: Trying to destroy buffer \"{}\" through a stale handle!", element.name));
		if (element.buffer.has_value())
		{
			VmaAllocationInfo alloc_info = element.buffer.value().get_allocation_info();
			VKTE_DEBUG("vkte: Destroying buffer \"{}\", Size: {}, Type: {}", element.name, alloc_info.size, alloc_info.memoryType);
			element.buffer.reset();
			element.generation++;
			free_buffer_slots.push_back(handle.id);
		}
		else
		{
			VKTE_ERROR("vkte: Trying to destroy already destroyed buffer!");
		}
	}
}

void Storage::clear()
{
	for (const std::unique_ptr<BufferElement>& buffer : buffers)
	{
		if (buffer->buffer.has_value()) VKTE_WARN("vkte: Buffer \"{}\" not destroyed! Cleaning up...", buffer->name);
	}
	buffers.clear();
	free_buffer_slots.clear();
	for (const std::unique_ptr<ImageElement>& image : images)
	{
		if (image->image.has_value()) VKTE_WARN("vkte: Image \"{}\" not destroyed! Cleaning up...", image->name);
	}
	images.clear();
	free_image_slots.clear();
}

Buffer& Storage::get_buffer(const ResourceHandle& handle)
{
	BufferElement& element = *buffers.at(handle.id);
	if (handle.generation != element.generation) VKTE_THROW(std::format("vkte: Trying to get buffer \"{}\" through a stale handle!", element.name));
	if (!element.buffer.has_value()) VKTE_THROW("vkte: Trying to get already destroyed buffer!");
	return element.buffer.value();
}

Image& Storage::get_image(const ResourceHandle& handle)
{
	ImageElement& element = *images.at(handle.id);
	if (handle.generation != element.generation) VKTE_THROW(std::format("vkte: Trying to get image \"{}\" through a stale handle!", element.name));
	if (!element.image.has_value()) VKTE_THROW("vkte: Trying to get already destroyed image!");
	return element.image.value();
}
} // namespace vkte
