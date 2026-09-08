#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include "vulkan/vulkan.hpp"
#include "vkte/buffer.hpp"
#include "vkte/image.hpp"
#include "vkte/vkte_log.hpp"

namespace vkte
{
class Storage
{
public:
	Storage(const VulkanMainContext& vmc, VulkanCommandContext& vcc);
	std::string get_memory_info();

	template<typename... Args>
	uint32_t add_buffer(const std::string& name, Args&&... args)
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
				buffers.at(buffer_names.at(name))->buffer.emplace(vmc, vcc, std::forward<Args>(args)...);
			}
		}
		else
		{
			buffers.push_back(std::make_unique<BufferElement>(name, std::make_optional<Buffer>(vmc, vcc, std::forward<Args>(args)...)));
			buffer_names.emplace(name, uint32_t(buffers.size() - 1));
		}
		const vk::Buffer& b = buffers.at(buffer_names.at(name))->buffer.value().get();
		vk::DebugUtilsObjectNameInfoEXT duoni(b.objectType, uint64_t(static_cast<vk::Buffer::CType>(b)), name.c_str());
		vmc.logical_device.get().setDebugUtilsObjectNameEXT(duoni);
		VmaAllocationInfo alloc_info = buffers.at(buffer_names.at(name))->buffer.value().get_allocation_info();
		VKTE_DEBUG("vkte: Creating buffer \"{}\", Size: {}, Type: {}", name, alloc_info.size, alloc_info.memoryType);
		return buffer_names.at(name);
	}

	template<typename... Args>
	uint32_t add_image(const std::string& name, Args&&... args)
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
				images.at(image_names.at(name))->image.emplace(vmc, vcc, std::forward<Args>(args)...);
			}
		}
		else
		{
			images.push_back(std::make_unique<ImageElement>(name, std::make_optional<Image>(vmc, vcc, std::forward<Args>(args)...)));
			image_names.emplace(name, images.size() - 1);
		}
		const vk::Image& i = images.at(image_names.at(name))->image.value().get_image();
		vk::DebugUtilsObjectNameInfoEXT duoni(i.objectType, uint64_t(static_cast<vk::Image::CType>(i)), name.c_str());
		vmc.logical_device.get().setDebugUtilsObjectNameEXT(duoni);
		VmaAllocationInfo alloc_info = images.at(image_names.at(name))->image.value().get_allocation_info();
		VKTE_DEBUG("vkte: Creating image \"{}\", Size: {}, Type: {}", name, alloc_info.size, alloc_info.memoryType);
		return image_names.at(name);
	}

	void destroy_buffer(uint32_t idx);
	void destroy_image(uint32_t idx);
	void destroy_buffer(const std::string& name);
	void destroy_image(const std::string& name);
	void clear();
	Buffer& get_buffer(uint32_t idx);
	Image& get_image(uint32_t idx);
	Buffer& get_buffer_by_name(const std::string& name);
	Image& get_image_by_name(const std::string& name);
	uint32_t get_buffer_index(const std::string& name) const;
	uint32_t get_image_index(const std::string& name) const;

private:
	const VulkanMainContext& vmc;
	VulkanCommandContext& vcc;

	struct BufferElement
	{
		BufferElement(const std::string& name, std::optional<Buffer>&& buffer) : name(name), buffer(std::move(buffer)) {}
		std::string name;
		std::optional<Buffer> buffer;
	};
	std::vector<std::unique_ptr<BufferElement>> buffers;
	std::unordered_map<std::string, uint32_t> buffer_names;

	struct ImageElement
	{
		ImageElement(const std::string& name, std::optional<Image>&& image) : name(name), image(std::move(image)) {}
		std::string name;
		std::optional<Image> image;
	};
	std::vector<std::unique_ptr<ImageElement>> images;
	std::unordered_map<std::string, uint32_t> image_names;
};
} // namespace vkte
