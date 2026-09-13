#include "vkte/device_feature_chain.hpp"

namespace vkte
{
DeviceFeatureChain build_required_feature_chain(const DeviceFeatures& features)
{
	DeviceFeatureChain chain;

	chain.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters = VK_TRUE;

	vk::PhysicalDeviceVulkan12Features& vulkan_12 = chain.get<vk::PhysicalDeviceVulkan12Features>();
	vulkan_12.bufferDeviceAddress = VK_TRUE;
	vulkan_12.scalarBlockLayout = VK_TRUE;
	vulkan_12.descriptorBindingPartiallyBound = VK_TRUE;
	vulkan_12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	vulkan_12.runtimeDescriptorArray = VK_TRUE;

	vk::PhysicalDeviceVulkan13Features& vulkan_13 = chain.get<vk::PhysicalDeviceVulkan13Features>();
	vulkan_13.synchronization2 = VK_TRUE;
	vulkan_13.shaderDemoteToHelperInvocation = VK_TRUE;
	vulkan_13.dynamicRendering = VK_TRUE;

	chain.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>().accelerationStructure = features.acceleration_structure;
	chain.get<vk::PhysicalDeviceRayQueryFeaturesKHR>().rayQuery = features.ray_query;
	chain.get<vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT>().extendedDynamicState3PolygonMode = features.dynamic_polygon_mode;

	vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT& atomic_float = chain.get<vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT>();
	atomic_float.shaderBufferFloat32Atomics = features.shader_atomic_float;
	atomic_float.shaderBufferFloat32AtomicAdd = features.shader_atomic_float;
	atomic_float.shaderSharedFloat32Atomics = features.shader_atomic_float;
	atomic_float.shaderSharedFloat32AtomicAdd = features.shader_atomic_float;
	atomic_float.shaderImageFloat32Atomics = features.shader_atomic_float;
	atomic_float.shaderImageFloat32AtomicAdd = features.shader_atomic_float;

	vk::PhysicalDeviceFeatures& core = chain.get<vk::PhysicalDeviceFeatures2>().features;
	core.samplerAnisotropy = VK_TRUE;
	core.sampleRateShading = VK_TRUE;
	core.fillModeNonSolid = VK_TRUE;
	core.fragmentStoresAndAtomics = VK_TRUE;
	core.wideLines = VK_TRUE;

	return chain;
}
} // namespace vkte
