#pragma once

#include "vulkan/vulkan.hpp"
#include "vkte/device_features.hpp"

namespace vkte
{
using DeviceFeatureChain = vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceAccelerationStructureFeaturesKHR, vk::PhysicalDeviceRayQueryFeaturesKHR, vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT, vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT>;

DeviceFeatureChain build_required_feature_chain(const DeviceFeatures& features);
bool is_feature_chain_satisfied(const DeviceFeatureChain& requested, const DeviceFeatureChain& supported);
} // namespace vkte
