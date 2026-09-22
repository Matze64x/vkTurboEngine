#pragma once

#include <vector>
#include "vulkan/vulkan.hpp"

namespace vkte
{
class Instance
{
public:
	Instance() = default;
	void construct(std::vector<const char*> required_extensions, std::vector<const char*> validation_layers, std::vector<vk::ValidationFeatureEnableEXT> validation_feature_enables = {});
	void destruct();
	const vk::Instance& get() const;
	std::vector<vk::PhysicalDevice> get_physical_devices() const;

private:
	vk::Instance instance;
	std::vector<const char*> extensions;
	std::vector<const char*> validation_layers;
};
} // namespace vkte
