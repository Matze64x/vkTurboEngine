#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include "vkte/buffer.hpp"
#include "vkte/image.hpp"
#include "vkte/resource_handles.hpp"

namespace vkte
{
class Engine;

class Storage
{
public:
	std::string get_memory_info();

	ResourceHandle add_buffer(const std::string& name, const Buffer::Settings& settings);
	ResourceHandle add_image(const std::string& name, const Image::Settings& settings);

	// Destroys a buffer or image, whichever the handle refers to (see ResourceHandle::is_image).
	void destroy(const ResourceHandle& handle);
	void clear();
	Buffer& get_buffer(const ResourceHandle& handle);
	Image& get_image(const ResourceHandle& handle);
	Buffer& get_buffer_by_name(const std::string& name);
	Image& get_image_by_name(const std::string& name);
	uint32_t get_buffer_index(const std::string& name) const;
	uint32_t get_image_index(const std::string& name) const;

private:
	friend class Engine;
	Storage(const VulkanMainContext& vmc, Command& command);

	const VulkanMainContext& vmc;
	Command& command;

	struct BufferElement
	{
		BufferElement(const std::string& name, const VulkanMainContext& vmc, Command& command, const Buffer::Settings& settings) : name(name), buffer(std::in_place, vmc, command, settings) {}
		std::string name;
		std::optional<Buffer> buffer;
	};
	std::vector<std::unique_ptr<BufferElement>> buffers;
	std::unordered_map<std::string, uint32_t> buffer_names;

	struct ImageElement
	{
		ImageElement(const std::string& name, const VulkanMainContext& vmc, Command& command, const Image::Settings& settings) : name(name), image(std::in_place, vmc, command, settings) {}
		std::string name;
		std::optional<Image> image;
	};
	std::vector<std::unique_ptr<ImageElement>> images;
	std::unordered_map<std::string, uint32_t> image_names;
};
} // namespace vkte
