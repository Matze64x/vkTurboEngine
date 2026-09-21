#pragma once

#include <cstdint>

namespace vkte
{
struct HandleBase
{
	using Type = uint32_t;
	static constexpr Type invalid_id = ~0u;
	Type id = invalid_id;
	Type generation = 0;
	bool valid() const { return id != invalid_id; }
};

struct DescriptorSetLayoutHandle : HandleBase {};
struct DescriptorSetsHandle : HandleBase {};
struct PipelineHandle : HandleBase {};
struct SemaphoreHandle : HandleBase {};
struct TimelineSemaphoreHandle : HandleBase {};
struct DeviceTimerHandle : HandleBase {};
struct AccelerationStructureBuilderHandle : HandleBase {};
struct CommandBufferHandle : HandleBase {};

// Memory Manager Handles
struct BufferHandle : HandleBase {};
struct ImageHandle : HandleBase {};

// Storage Handle
struct ResourceHandle : HandleBase
{
	bool is_image = false;

	ResourceHandle() = default;
	ResourceHandle(uint32_t id, uint32_t generation, bool is_image) : HandleBase{id, generation}, is_image(is_image) {}
};
} // namespace vkte
