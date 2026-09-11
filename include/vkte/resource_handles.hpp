#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace vkte
{
struct HandleBase
{
	using Type = uint32_t;
	static constexpr Type invalid_id = ~0u;
	Type id = invalid_id;
	bool valid() const { return id != invalid_id; }
};

struct DescriptorSetLayoutHandle : HandleBase {};
struct DescriptorSetsHandle : HandleBase {};
struct PipelineHandle : HandleBase {};
struct SemaphoreHandle : HandleBase {};
struct FenceHandle : HandleBase {};
struct DeviceTimerHandle : HandleBase {};
struct AccelerationStructureBuilderHandle : HandleBase {};
struct CommandBufferHandle : HandleBase {};

struct ResourceHandle : HandleBase
{
	// ID takes precedence over name if both are set.
	std::string name;
	bool is_image = false;

	ResourceHandle() = default;
	ResourceHandle(std::string name, bool is_image) : name(std::move(name)), is_image(is_image) {}
	ResourceHandle(uint32_t id, std::string name, bool is_image) : HandleBase{id}, name(std::move(name)), is_image(is_image) {}

	bool valid() const { return id != invalid_id || !name.empty(); }
};
} // namespace vkte
