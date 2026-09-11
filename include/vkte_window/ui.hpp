#pragma once

#include <string>
#include "vulkan/vulkan.hpp"

namespace vkte
{
class Engine;
class Swapchain;
class VulkanMainContext;

class UI
{
public:
	void new_frame(const std::string& title);
	void end_frame(vk::CommandBuffer& cb);

private:
	friend class Engine;
	void construct(const vkte::VulkanMainContext& vmc, const vkte::Swapchain& swapchain);
	void destruct(const vkte::VulkanMainContext& vmc);

	vk::DescriptorPool imgui_pool;
};
} // namespace vkte
