#pragma once

#include <utility>
#include "vulkan/vulkan.hpp"

#include "vkte/queue_families.hpp"
#include "vkte/vkte_log.hpp"
#include "vkte/command.hpp"
#include "vkte/vulkan_main_context.hpp"

namespace vkte
{
class Buffer
{
public:
	template<class T>
	Buffer(const VulkanMainContext& vmc, Command& command, const T* data, std::size_t elements, vk::BufferUsageFlags usage_flags, bool device_local, Queues queues) : Buffer(vmc, command, sizeof(T) * elements, usage_flags, device_local, queues)
	{
		element_count = elements;
		update_data(data, elements);
	}

	template<class T>
	Buffer(const VulkanMainContext& vmc, Command& command, const std::vector<T>& data, vk::BufferUsageFlags usage_flags, bool device_local, Queues queues) : Buffer(vmc, command, data.data(), data.size(), usage_flags, device_local, queues)
	{}

	Buffer(const VulkanMainContext& vmc, Command& command, std::size_t byte_size, vk::BufferUsageFlags usage_flags, bool device_local, Queues queues) : vmc(vmc), command(command), device_local(device_local), byte_size(byte_size)
	{
		if (device_local)
		{
			std::tie(buffer, vmaa) = create_buffer((usage_flags | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc), {}, byte_size, device_local, queues);
		}
		else
		{
			std::tie(buffer, vmaa) = create_buffer(usage_flags, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, byte_size, device_local, queues);
		}
	}

	void destruct()
	{
		vmaDestroyBuffer(vmc.va, buffer, vmaa);
	}

	const vk::Buffer& get() const
	{
		return buffer;
	}

	uint64_t get_element_count() const
	{
		return element_count;
	}

	uint64_t get_byte_size() const
	{
		return byte_size;
	}

	void update_data_bytes(int constant, std::size_t byte_count)
	{
		VKTE_ASSERT(byte_count <= byte_size, "vkte: Data is larger than buffer!");

		if (device_local)
		{
			auto [staging_buffer, staging_vmaa] = create_buffer((vk::BufferUsageFlagBits::eTransferSrc), VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, byte_count, true, QueueFamilyFlags::Transfer);
			void* mapped_mem;
			vmaMapMemory(vmc.va, staging_vmaa, &mapped_mem);
			memset(mapped_mem, constant, byte_count);
			vmaUnmapMemory(vmc.va, staging_vmaa);

			vk::CommandBuffer& cb = command.get_one_time_transfer_buffer();

			vk::BufferCopy copy_region;
			copy_region.srcOffset = 0;
			copy_region.dstOffset = 0;
			copy_region.size = byte_count;
			cb.copyBuffer(staging_buffer, buffer, copy_region);
			command.submit_transfer(cb, true);

			vmaDestroyBuffer(vmc.va, staging_buffer, staging_vmaa);
		}
		else
		{
			void* mapped_mem;
			vmaMapMemory(vmc.va, vmaa, &mapped_mem);
			memset(mapped_mem, constant, byte_count);
			vmaUnmapMemory(vmc.va, vmaa);
		}
	}

	void update_data_bytes(const void* data, std::size_t byte_count, std::size_t offset = 0)
	{
		VKTE_ASSERT(byte_count <= byte_size, "vkte: Data is larger than buffer!");
		VKTE_ASSERT(offset + byte_count <= byte_size, "vkte: Trying to write outside of the buffer!");

		if (device_local)
		{
			auto [staging_buffer, staging_vmaa] = create_buffer((vk::BufferUsageFlagBits::eTransferSrc), VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, byte_count, true, QueueFamilyFlags::Transfer);
			void* mapped_mem;
			vmaMapMemory(vmc.va, staging_vmaa, &mapped_mem);
			memcpy(mapped_mem, data, byte_count);
			vmaUnmapMemory(vmc.va, staging_vmaa);

			vk::CommandBuffer& cb = command.get_one_time_transfer_buffer();

			vk::BufferCopy copy_region;
			copy_region.srcOffset = 0;
			copy_region.dstOffset = offset;
			copy_region.size = byte_count;
			cb.copyBuffer(staging_buffer, buffer, copy_region);
			command.submit_transfer(cb, true);

			vmaDestroyBuffer(vmc.va, staging_buffer, staging_vmaa);
		}
		else
		{
			void* mapped_mem;
			vmaMapMemory(vmc.va, vmaa, &mapped_mem);
			memcpy((uint8_t*)(mapped_mem) + offset, data, byte_count);
			vmaUnmapMemory(vmc.va, vmaa);
		}
	}

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

	void obtain_data_bytes(void* data, std::size_t byte_count)
	{
		VKTE_ASSERT(byte_count <= byte_size, "vkte: Cannot get more bytes than size of buffer!");

		if (device_local)
		{
			auto [staging_buffer, staging_vmaa] = create_buffer((vk::BufferUsageFlagBits::eTransferDst), VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, byte_count, false, QueueFamilyFlags::Transfer);

			vk::CommandBuffer& cb = command.get_one_time_transfer_buffer();
			vk::BufferCopy copy_region;
			copy_region.srcOffset = 0;
			copy_region.dstOffset = 0;
			copy_region.size = byte_count;
			cb.copyBuffer(buffer, staging_buffer, copy_region);
			command.submit_transfer(cb, true);

			void* mapped_mem;
			vmaMapMemory(vmc.va, staging_vmaa, &mapped_mem);
			memcpy(data, mapped_mem, byte_count);
			vmaUnmapMemory(vmc.va, staging_vmaa);

			vmaDestroyBuffer(vmc.va, staging_buffer, staging_vmaa);
		}
		else
		{
			void* mapped_mem;
			vmaMapMemory(vmc.va, vmaa, &mapped_mem);
			memcpy(data, mapped_mem, byte_count);
			vmaUnmapMemory(vmc.va, vmaa);
		}
	}

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

	vk::DeviceAddress get_device_address()
	{
		vk::BufferDeviceAddressInfoKHR buffer_device_adress_i;
		buffer_device_adress_i.buffer = buffer;
		return vmc.logical_device.get().getBufferAddress(buffer_device_adress_i);
	}

	VmaAllocationInfo get_allocation_info() const
	{
		VmaAllocationInfo alloc_info;
		vmaGetAllocationInfo(vmc.va, vmaa, &alloc_info);
		return alloc_info;
	}

	void* pNext = nullptr;

private:
	std::pair<vk::Buffer, VmaAllocation> create_buffer(vk::BufferUsageFlags usage_flags, VmaAllocationCreateFlags vma_flags, std::size_t byte_size, bool device_local, Queues queues)
	{
		std::vector<uint32_t> queue_indices = vmc.queue_families.get(queues);
		vk::BufferCreateInfo bci;
		bci.size = byte_size;
		bci.usage = usage_flags;
		bci.sharingMode = queue_indices.size() == 1 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent;
		bci.flags = {};
		bci.queueFamilyIndexCount = queue_indices.size();
		bci.pQueueFamilyIndices = queue_indices.data();
		VmaAllocationCreateInfo vaci{};
		vaci.usage = device_local ? VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE : VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
		vaci.flags = vma_flags;
		VkBuffer local_buffer;
		VmaAllocation local_vmaa;
		vmaCreateBuffer(vmc.va, (VkBufferCreateInfo*) (&bci), &vaci, (&local_buffer), &local_vmaa, nullptr);

		return std::make_pair(vk::Buffer(local_buffer), local_vmaa);
	}

	const VulkanMainContext& vmc;
	Command& command;
	bool device_local;
	uint64_t byte_size;
	uint64_t element_count;
	vk::Buffer buffer;
	VmaAllocation vmaa;
};
} // namespace vkte
