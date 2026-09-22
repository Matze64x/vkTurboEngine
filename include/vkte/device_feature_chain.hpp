#pragma once

#include "vulkan/vulkan.hpp"
#include "vkte/device_features.hpp"

namespace vkte
{
using DeviceFeatureChain = vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceVulkan14Features, vk::PhysicalDeviceAccelerationStructureFeaturesKHR, vk::PhysicalDeviceRayQueryFeaturesKHR, vk::PhysicalDeviceRayTracingPipelineFeaturesKHR, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT, vk::PhysicalDeviceExtendedDynamicState2FeaturesEXT, vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT, vk::PhysicalDeviceVertexInputDynamicStateFeaturesEXT, vk::PhysicalDeviceShaderObjectFeaturesEXT, vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT>;

DeviceFeatureChain build_required_feature_chain(const DeviceFeatures& features);
DeviceFeatureChain query_supported_feature_chain(vk::PhysicalDevice p_device);
bool is_feature_chain_satisfied(const DeviceFeatureChain& requested, const DeviceFeatureChain& supported);
} // namespace vkte
