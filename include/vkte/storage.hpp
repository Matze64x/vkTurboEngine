#pragma once

#include <memory>
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
		uint32_t generation = 0;
	};
	std::vector<std::unique_ptr<BufferElement>> buffers;
	std::vector<uint32_t> free_buffer_slots;

	struct ImageElement
	{
		ImageElement(const std::string& name, const VulkanMainContext& vmc, Command& command, const Image::Settings& settings) : name(name), image(std::in_place, vmc, command, settings) {}
		std::string name;
		std::optional<Image> image;
		uint32_t generation = 0;
	};
	std::vector<std::unique_ptr<ImageElement>> images;
	std::vector<uint32_t> free_image_slots;
};
} // namespace vkte
