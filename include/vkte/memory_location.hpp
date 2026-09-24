#pragma once

#include <optional>
#include "vk_mem_alloc.h"

namespace vkte
{
enum class MemoryLocation
{
	DeviceLocal,
	HostVisible,
	BARFallbackDeviceLocal,
	BARFallbackHostVisible,
	HostCached,
};

inline VmaAllocationCreateInfo to_vma_allocation_create_info(MemoryLocation location)
{
	VmaAllocationCreateInfo vaci{};
	switch (location)
	{
		case MemoryLocation::DeviceLocal:
			vaci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
			break;
		case MemoryLocation::HostVisible:
			vaci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
			vaci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
			break;
		case MemoryLocation::BARFallbackDeviceLocal:
			vaci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
			vaci.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
			break;
		case MemoryLocation::BARFallbackHostVisible:
			vaci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
			vaci.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
			vaci.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			break;
		case MemoryLocation::HostCached:
			vaci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
			vaci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
			vaci.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			break;
	}
	return vaci;
}

inline std::optional<MemoryLocation> get_fallback_location(MemoryLocation location)
{
	if (location == MemoryLocation::BARFallbackDeviceLocal) return MemoryLocation::DeviceLocal;
	return std::nullopt;
}
} // namespace vkte
