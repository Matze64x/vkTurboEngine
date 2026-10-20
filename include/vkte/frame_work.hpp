#pragma once

#include <cstdint>
#include "vulkan/vulkan.hpp"

namespace vkte
{
// Work that must run before a frame's own work. Engine::begin_frame() records it into a compute queue before recording any of its own work but after waiting for the frame index.
class FrameWork
{
public:
	virtual ~FrameWork() = default;
	virtual bool has_frame_work() const = 0;
	// the frame's previous use is over, so what exists per frame in flight can be reused
	virtual void record_frame_work(vk::CommandBuffer& cb, uint32_t frame_index) = 0;
};
} // namespace vkte
