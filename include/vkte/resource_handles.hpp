#pragma once

#include <cstdint>

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
} // namespace vkte
