#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <vector>
#include "vulkan/vulkan.hpp"
#include "vk_mem_alloc.h"

#include "vkte/memory_location.hpp"
#include "vkte/queue_families.hpp"

namespace vkte
{
class VulkanMainContext;
class Command;

class Buffer
{
public:
	struct Settings
	{
		std::size_t byte_size = 0;
		std::span<const std::byte> initial_data{};
		std::size_t element_count = 0;
		vk::BufferUsageFlags usage_flags{};
		MemoryLocation location = MemoryLocation::DeviceLocal;
		Queues queues{};
		vk::DeviceSize min_alignment = 0;
	};

	Buffer(const VulkanMainContext& vmc, Command& command, const Settings& settings);
	~Buffer();
	Buffer(const Buffer&) = delete;
	Buffer& operator=(const Buffer&) = delete;
	Buffer(Buffer&&) = delete;
	Buffer& operator=(Buffer&&) = delete;
	const vk::Buffer& get() const { return buffer; }
	uint64_t get_element_count() const { return element_count; }
	uint64_t get_byte_size() const { return byte_size; }
	void fill_bytes(int constant, std::size_t byte_count);
	void update_data_bytes(const void* data, std::size_t byte_count, std::size_t offset = 0);

	template<class T>
	void update_data(const T* data, std::size_t elements, std::size_t offset = 0)
	{
		update_data_bytes(data, sizeof(T) * elements, sizeof(T) * offset);
	}

	template<class T>
	void update_data(const std::vector<T>& data)
	{
		update_data(data.data(), data.size());
	}

	template<class T>
	void update_data(const T& data)
	{
		update_data_bytes(&data, sizeof(T));
	}

	void obtain_data_bytes(void* data, std::size_t byte_count);

	template<class T>
	std::vector<T> obtain_data(std::size_t element_count)
	{
		std::vector<T> data(element_count);
		obtain_data_bytes(data.data(), sizeof(T) * element_count);
		return data;
	}

	template<class T>
	std::vector<T> obtain_all_data()
	{
		std::vector<T> data(element_count);
		obtain_data_bytes(data.data(), sizeof(T) * element_count);
		return data;
	}

	template<class T>
	void obtain_all_data(std::vector<T>& output)
	{
		if (output.size() < element_count) output.resize(element_count);
		obtain_data_bytes(output.data(), sizeof(T) * element_count);
	}

	template<class T>
	T obtain_first_element()
	{
		T data;
		obtain_data_bytes(&data, sizeof(T));
		return data;
	}

	vk::DeviceAddress get_device_address();
	VmaAllocationInfo get_allocation_info() const;

	void* pNext = nullptr;

private:
	enum class TransferDirection
	{
		HostToBuffer,
		BufferToHost,
	};

	void apply_memory_operation(std::size_t offset, std::size_t byte_count, TransferDirection direction, const std::function<void(void*)>& host_op);

	const VulkanMainContext& vmc;
	Command& command;
	bool host_visible = false;
	uint64_t byte_size;
	uint64_t element_count;
	vk::Buffer buffer;
	VmaAllocation vmaa;
};
} // namespace vkte
