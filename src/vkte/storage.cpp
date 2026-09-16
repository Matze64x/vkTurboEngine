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
	if (buffer_names.contains(name))
	{
		if (buffers.at(buffer_names.at(name))->buffer.has_value())
		{
			// buffer name is already taken by an existing buffer
			VKTE_WARN("vkte: Duplicate buffer name: {}", name);
		}
		else
		{
			// buffer name exists but the corresponding buffer got deleted; so, reuse the name
			buffers.at(buffer_names.at(name))->name = name;
			buffers.at(buffer_names.at(name))->buffer.emplace(vmc, command, settings);
		}
	}
	else
	{
		buffers.push_back(std::make_unique<BufferElement>(name, vmc, command, settings));
		buffer_names.emplace(name, uint32_t(buffers.size() - 1));
	}
	const vk::Buffer& b = buffers.at(buffer_names.at(name))->buffer.value().get();
	vk::DebugUtilsObjectNameInfoEXT duoni(b.objectType, uint64_t(static_cast<vk::Buffer::CType>(b)), name.c_str());
	vmc.logical_device.get().setDebugUtilsObjectNameEXT(duoni);
	VmaAllocationInfo alloc_info = buffers.at(buffer_names.at(name))->buffer.value().get_allocation_info();
	VKTE_DEBUG("vkte: Creating buffer \"{}\", Size: {}, Type: {}", name, alloc_info.size, alloc_info.memoryType);
	return ResourceHandle(buffer_names.at(name), name, false);
}

ResourceHandle Storage::add_image(const std::string& name, const Image::Settings& settings)
{
	if (image_names.contains(name))
	{
		if (images.at(image_names.at(name))->image.has_value())
		{
			// image name is already taken by an existing image
			VKTE_WARN("vkte: Duplicate image name: {}", name);
		}
		else
		{
			// image name exists but the corresponding image got deleted; so, reuse the name
			images.at(image_names.at(name))->name = name;
			images.at(image_names.at(name))->image.emplace(vmc, command, settings);
		}
	}
	else
	{
		images.push_back(std::make_unique<ImageElement>(name, vmc, command, settings));
		image_names.emplace(name, uint32_t(images.size() - 1));
	}
	const vk::Image& i = images.at(image_names.at(name))->image.value().get_image();
	vk::DebugUtilsObjectNameInfoEXT duoni(i.objectType, uint64_t(static_cast<vk::Image::CType>(i)), name.c_str());
	vmc.logical_device.get().setDebugUtilsObjectNameEXT(duoni);
	VmaAllocationInfo alloc_info = images.at(image_names.at(name))->image.value().get_allocation_info();
	VKTE_DEBUG("vkte: Creating image \"{}\", Size: {}, Type: {}", name, alloc_info.size, alloc_info.memoryType);
	return ResourceHandle(image_names.at(name), name, true);
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
		const uint32_t idx = handle.id != ResourceHandle::invalid_id ? handle.id : get_image_index(handle.name);
		if (images.at(idx)->image.has_value())
		{
			VmaAllocationInfo alloc_info = images.at(idx)->image.value().get_allocation_info();
			VKTE_DEBUG("vkte: Destroying image \"{}\", Size: {}, Type: {}", images.at(idx)->name, alloc_info.size, alloc_info.memoryType);
			images.at(idx)->image.reset();
		}
		else
		{
			VKTE_ERROR("vkte: Trying to destroy already destroyed image!");
		}
	}
	else
	{
		const uint32_t idx = handle.id != ResourceHandle::invalid_id ? handle.id : get_buffer_index(handle.name);
		if (buffers.at(idx)->buffer.has_value())
		{
			VmaAllocationInfo alloc_info = buffers.at(idx)->buffer.value().get_allocation_info();
			VKTE_DEBUG("vkte: Destroying buffer \"{}\", Size: {}, Type: {}", buffers.at(idx)->name, alloc_info.size, alloc_info.memoryType);
			buffers.at(idx)->buffer.reset();
		}
		else
		{
			VKTE_ERROR("vkte: Trying to destroy already destroyed buffer!");
		}
	}
}

void Storage::clear()
{
	for (const std::pair<std::string, uint32_t>& buffer : buffer_names)
	{
		if (buffers[buffer.second]->buffer.has_value()) VKTE_WARN("vkte: Buffer \"{}\" not destroyed! Cleaning up...", buffer.first);
	}
	buffers.clear();
	buffer_names.clear();
	for (const std::pair<std::string, uint32_t>& image : image_names)
	{
		if (images[image.second]->image.has_value()) VKTE_WARN("vkte: Image \"{}\" not destroyed! Cleaning up...", image.first);
	}
	images.clear();
	image_names.clear();
}

Buffer& Storage::get_buffer(const ResourceHandle& handle)
{
	const uint32_t idx = handle.id != ResourceHandle::invalid_id ? handle.id : get_buffer_index(handle.name);
	if (!buffers.at(idx)->buffer.has_value()) VKTE_THROW("vkte: Trying to get already destroyed buffer!");
	return buffers.at(idx)->buffer.value();
}

Image& Storage::get_image(const ResourceHandle& handle)
{
	const uint32_t idx = handle.id != ResourceHandle::invalid_id ? handle.id : get_image_index(handle.name);
	if (!images.at(idx)->image.has_value()) VKTE_THROW("vkte: Trying to get already destroyed image!");
	return images.at(idx)->image.value();
}

Buffer& Storage::get_buffer_by_name(const std::string& name)
{
	return get_buffer(ResourceHandle(name, false));
}

Image& Storage::get_image_by_name(const std::string& name)
{
	return get_image(ResourceHandle(name, true));
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
} // namespace vkte
