#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace vkte
{
struct DescriptorSetLayoutHandle
{
	uint32_t index = ~0u;
	bool valid() const { return index != ~0u; }
};

struct DescriptorSetsHandle
{
	uint32_t index = ~0u;
	bool valid() const { return index != ~0u; }
};

struct PipelineHandle
{
	uint32_t index = ~0u;
	bool valid() const { return index != ~0u; }
};

struct SemaphoreHandle
{
	uint32_t index = ~0u;
	bool valid() const { return index != ~0u; }
};

struct FenceHandle
{
	uint32_t index = ~0u;
	bool valid() const { return index != ~0u; }
};

struct DeviceTimerHandle
{
	uint32_t index = ~0u;
	bool valid() const { return index != ~0u; }
};

struct AccelerationStructureBuilderHandle
{
	uint32_t index = ~0u;
	bool valid() const { return index != ~0u; }
};

struct ResourceHandle
{
	static constexpr uint32_t invalid_id = ~0u;

	// ID takes precedence over name if both are set.
	uint32_t id = invalid_id;
	std::string name;
	bool is_image = false;

	ResourceHandle() = default;
	ResourceHandle(std::string name, bool is_image) : name(std::move(name)), is_image(is_image) {}
	ResourceHandle(uint32_t id, std::string name, bool is_image) : id(id), name(std::move(name)), is_image(is_image) {}

	bool valid() const { return id != invalid_id || !name.empty(); }
};
} // namespace vkte
