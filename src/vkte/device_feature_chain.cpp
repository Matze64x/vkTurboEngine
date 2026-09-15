#include "vkte/device_feature_chain.hpp"

#include <cstddef>
#include <cstring>

namespace vkte
{
namespace
{
// every vk::PhysicalDevice*Features struct is its header (sType, pNext) followed by nothing but vk::Bool32 fields.
// So instead of naming every feature bit, we can just compare the raw bits after the header.
// Set struct to 0 before to interpret padding as additional bool set to false on both sides.
template<class T>
void zero_fields(T& s)
{
	constexpr std::size_t header_size = offsetof(T, pNext) + sizeof(T::pNext);
	std::memset(reinterpret_cast<std::byte*>(&s) + header_size, 0, sizeof(T) - header_size);
}

template<class... Ts>
void zero_fields(vk::StructureChain<Ts...>& chain)
{
	(zero_fields(chain.template get<Ts>()), ...);
}

template<class T>
bool bool32_fields_satisfied(const T& requested, const T& supported)
{
	constexpr std::size_t header_size = offsetof(T, pNext) + sizeof(T::pNext);
	constexpr std::size_t count = (sizeof(T) - header_size) / sizeof(vk::Bool32);
	const vk::Bool32* requested_fields = reinterpret_cast<const vk::Bool32*>(reinterpret_cast<const std::byte*>(&requested) + header_size);
	const vk::Bool32* supported_fields = reinterpret_cast<const vk::Bool32*>(reinterpret_cast<const std::byte*>(&supported) + header_size);
	for (std::size_t i = 0; i < count; ++i)
	{
		if (requested_fields[i] && !supported_fields[i]) return false;
	}
	return true;
}

template<class... Ts>
bool structure_chain_satisfied(const vk::StructureChain<Ts...>& requested, const vk::StructureChain<Ts...>& supported)
{
	return (bool32_fields_satisfied(requested.template get<Ts>(), supported.template get<Ts>()) && ...);
}
} // namespace

bool is_feature_chain_satisfied(const DeviceFeatureChain& requested, const DeviceFeatureChain& supported)
{
	return structure_chain_satisfied(requested, supported);
}

DeviceFeatureChain query_supported_feature_chain(vk::PhysicalDevice p_device)
{
	DeviceFeatureChain chain;
	zero_fields(chain);
	p_device.getFeatures2(&chain.get<vk::PhysicalDeviceFeatures2>());
	return chain;
}

DeviceFeatureChain build_required_feature_chain(const DeviceFeatures& features)
{
	DeviceFeatureChain chain;
	zero_fields(chain);

	chain.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters = VK_TRUE;

	vk::PhysicalDeviceVulkan12Features& vulkan_12 = chain.get<vk::PhysicalDeviceVulkan12Features>();
	vulkan_12.bufferDeviceAddress = VK_TRUE;
	vulkan_12.scalarBlockLayout = VK_TRUE;
	vulkan_12.descriptorBindingPartiallyBound = VK_TRUE;
	vulkan_12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	vulkan_12.runtimeDescriptorArray = VK_TRUE;
	vulkan_12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
	vulkan_12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
	vulkan_12.descriptorBindingVariableDescriptorCount = VK_TRUE;

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
	core.shaderInt64 = VK_TRUE;

	return chain;
}
} // namespace vkte
