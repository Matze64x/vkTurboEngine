#pragma once

#include <optional>
#include "vulkan/vulkan.hpp"

#include "vkte/device_features.hpp"
#include "vkte/extensions_handler.hpp"
#include "vkte/instance.hpp"

namespace vkte
{
class PhysicalDevice
{
public:
	PhysicalDevice() = default;
	void construct(const Instance& instance, const std::vector<const char*>& required_extensions, const DeviceFeatures& features, const std::optional<vk::SurfaceKHR>& surface);
	vk::PhysicalDevice get() const;
	const std::vector<const char*>& get_extensions() const;

private:
	vk::PhysicalDevice physical_device;
	ExtensionsHandler extensions_handler;

	bool is_device_suitable(uint32_t idx, const vk::PhysicalDevice p_device, const DeviceFeatures& features, const std::optional<vk::SurfaceKHR>& surface);
	bool check_feature_support(vk::PhysicalDevice p_device, const DeviceFeatures& features) const;
};
} // namespace vkte
