#include "vkte/buffer.hpp"

#include <cstring>
#include <format>
#include "vkte/command.hpp"
#include "vkte/vkte_log.hpp"
#include "vkte/vulkan_main_context.hpp"

namespace vkte
{
namespace
{
bool try_create_buffer(const VulkanMainContext& vmc, vk::BufferUsageFlags usage_flags, VmaAllocationCreateInfo vaci, std::size_t byte_size, Queues queues, vk::DeviceSize min_alignment, vk::Buffer& out_buffer, VmaAllocation& out_vmaa)
{
	std::vector<uint32_t> queue_indices = vmc.queue_families.get(queues);
	vk::BufferCreateInfo bci;
	bci.size = byte_size;
	bci.usage = usage_flags;
	bci.sharingMode = queue_indices.size() == 1 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent;
	bci.flags = {};
	bci.queueFamilyIndexCount = queue_indices.size();
	bci.pQueueFamilyIndices = queue_indices.data();
	vaci.minAlignment = min_alignment;
	VkBuffer local_buffer;
	VmaAllocation local_vmaa;
	if (vmaCreateBuffer(vmc.va, (VkBufferCreateInfo*) (&bci), &vaci, &local_buffer, &local_vmaa, nullptr) != VK_SUCCESS) return false;
	out_buffer = vk::Buffer(local_buffer);
	out_vmaa = local_vmaa;
	return true;
}
} // namespace

Buffer::Buffer(const VulkanMainContext& vmc, Command& command, const Settings& settings) : vmc(vmc), command(command), byte_size(settings.initial_data.empty() ? settings.byte_size : settings.initial_data.size()), element_count(settings.element_count)
{
	VKTE_ASSERT(byte_size > 0, "vkte: Buffer::Settings must specify either byte_size or initial_data, both were empty");
	VKTE_ASSERT(settings.byte_size == 0 || settings.initial_data.empty() || settings.byte_size == settings.initial_data.size(), std::format("vkte: Buffer::Settings::byte_size ({}) does not match initial_data.size() ({})", settings.byte_size, settings.initial_data.size()));
	VKTE_ASSERT(settings.usage_flags, "vkte: Buffer::Settings::usage_flags must not be empty");
	VKTE_ASSERT(settings.queues, "vkte: Buffer::Settings::queues must not be empty");

	auto apply_device_local_usage_flags = [](MemoryLocation loc, vk::BufferUsageFlags base_usage_flags) {
		return loc == MemoryLocation::DeviceLocal ? (base_usage_flags | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc) : base_usage_flags;
	};

	MemoryLocation actual_location = settings.location;
	const vk::BufferUsageFlags base_usage_flags = settings.usage_flags | vk::BufferUsageFlagBits::eShaderDeviceAddress;
	if (!try_create_buffer(vmc, apply_device_local_usage_flags(actual_location, base_usage_flags), to_vma_allocation_create_info(actual_location), byte_size, settings.queues, settings.min_alignment, buffer, vmaa))
	{
		const std::optional<MemoryLocation> fallback = get_fallback_location(actual_location);
		VKTE_ASSERT(fallback.has_value(), "vkte: Failed to allocate buffer in the requested MemoryLocation");
		actual_location = *fallback;
		VKTE_ASSERT(try_create_buffer(vmc, apply_device_local_usage_flags(actual_location, base_usage_flags), to_vma_allocation_create_info(actual_location), byte_size, settings.queues, settings.min_alignment, buffer, vmaa), "vkte: Failed to allocate buffer in its fallback MemoryLocation as well");
	}
	host_visible = actual_location != MemoryLocation::DeviceLocal;

	if (!settings.initial_data.empty()) update_data_bytes(settings.initial_data.data(), settings.initial_data.size());
}

Buffer::~Buffer()
{
	vmaDestroyBuffer(vmc.va, buffer, vmaa);
}

void Buffer::fill_bytes(int constant, std::size_t byte_count)
{
	VKTE_ASSERT(byte_count <= byte_size, "vkte: Data is larger than buffer!");
	apply_memory_operation(0, byte_count, TransferDirection::HostToBuffer, [&](void* mapped_mem) { memset(mapped_mem, constant, byte_count); });
}

void Buffer::update_data_bytes(const void* data, std::size_t byte_count, std::size_t offset)
{
	VKTE_ASSERT(byte_count <= byte_size, "vkte: Data is larger than buffer!");
	VKTE_ASSERT(offset + byte_count <= byte_size, "vkte: Trying to write outside of the buffer!");
	apply_memory_operation(offset, byte_count, TransferDirection::HostToBuffer, [&](void* mapped_mem) { memcpy(mapped_mem, data, byte_count); });
}

void Buffer::obtain_data_bytes(void* data, std::size_t byte_count)
{
	VKTE_ASSERT(byte_count <= byte_size, "vkte: Cannot get more bytes than size of buffer!");
	apply_memory_operation(0, byte_count, TransferDirection::BufferToHost, [&](void* mapped_mem) { memcpy(data, mapped_mem, byte_count); });
}

void Buffer::apply_memory_operation(std::size_t offset, std::size_t byte_count, TransferDirection direction, const std::function<void(void*)>& host_op)
{
	if (host_visible)
	{
		void* mapped_mem;
		vmaMapMemory(vmc.va, vmaa, &mapped_mem);
		host_op((uint8_t*) (mapped_mem) + offset);
		vmaUnmapMemory(vmc.va, vmaa);
		return;
	}

	const vk::BufferUsageFlagBits staging_usage = direction == TransferDirection::HostToBuffer ? vk::BufferUsageFlagBits::eTransferSrc : vk::BufferUsageFlagBits::eTransferDst;
	vk::Buffer staging_buffer;
	VmaAllocation staging_vmaa;
	VKTE_ASSERT(try_create_buffer(vmc, staging_usage, to_vma_allocation_create_info(MemoryLocation::HostVisible), byte_count, QueueFamilyFlags::Transfer, 0, staging_buffer, staging_vmaa), "vkte: Failed to allocate staging buffer");

	if (direction == TransferDirection::BufferToHost)
	{
		vk::CommandBuffer& cb = command.get_one_time_transfer_buffer();
		vk::BufferCopy copy_region;
		copy_region.srcOffset = offset;
		copy_region.dstOffset = 0;
		copy_region.size = byte_count;
		cb.copyBuffer(buffer, staging_buffer, copy_region);
		command.submit_transfer(cb, true);
	}

	void* mapped_mem;
	vmaMapMemory(vmc.va, staging_vmaa, &mapped_mem);
	host_op(mapped_mem);
	vmaUnmapMemory(vmc.va, staging_vmaa);

	if (direction == TransferDirection::HostToBuffer)
	{
		vk::CommandBuffer& cb = command.get_one_time_transfer_buffer();
		vk::BufferCopy copy_region;
		copy_region.srcOffset = 0;
		copy_region.dstOffset = offset;
		copy_region.size = byte_count;
		cb.copyBuffer(staging_buffer, buffer, copy_region);
		command.submit_transfer(cb, true);
	}

	vmaDestroyBuffer(vmc.va, staging_buffer, staging_vmaa);
}

vk::DeviceAddress Buffer::get_device_address()
{
	vk::BufferDeviceAddressInfoKHR buffer_device_adress_i;
	buffer_device_adress_i.buffer = buffer;
	return vmc.logical_device.get().getBufferAddress(buffer_device_adress_i);
}

VmaAllocationInfo Buffer::get_allocation_info() const
{
	VmaAllocationInfo alloc_info;
	vmaGetAllocationInfo(vmc.va, vmaa, &alloc_info);
	return alloc_info;
}
} // namespace vkte
