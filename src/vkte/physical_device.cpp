#include "vkte/physical_device.hpp"

#include <iostream>
#include <string>
#include <unordered_set>

#include "vkte/device_feature_chain.hpp"
#include "vkte/vkte_log.hpp"

namespace vkte
{
void PhysicalDevice::construct(const Instance& instance, const std::vector<const char*>& required_extensions, const DeviceFeatures& features, const std::optional<vk::SurfaceKHR>& surface)
{
	extensions_handler.add_extensions(required_extensions);

	std::vector<vk::PhysicalDevice> physical_devices = instance.get_physical_devices();
	std::unordered_set<uint32_t> suitable_p_devices;
	for (uint32_t i = 0; i < physical_devices.size(); ++i)
	{
		if (is_device_suitable(i, physical_devices[i], features, surface))
		{
			suitable_p_devices.insert(i);
		}
	}
	if (suitable_p_devices.size() > 1)
	{
		uint32_t pd_idx = 0;
		do
		{
			std::cerr << "Select one of the suitable GPUs by typing the number" << std::endl;
			std::cin >> pd_idx;
		} while (!suitable_p_devices.contains(pd_idx));
		physical_device = physical_devices[pd_idx];
	}
	else if (suitable_p_devices.size() == 1)
	{
		VKTE_INFO("vkte: Only one suitable GPU. Using this one.");
		physical_device = physical_devices[*(suitable_p_devices.begin())];
	}
	else
	{
		VKTE_THROW("vkte: No suitable GPU found!");
	}
	vk::PhysicalDeviceProperties pdp = physical_device.getProperties();
	VKTE_INFO("vkte: GPU: {}", std::string(pdp.deviceName.data()));
}

vk::PhysicalDevice PhysicalDevice::get() const
{
	return physical_device;
}

const std::vector<const char*>& PhysicalDevice::get_extensions() const
{
	return extensions_handler.get_extensions();
}

bool is_swapchain_supported(const vk::PhysicalDevice p_device, const vk::SurfaceKHR& surface)
{
	std::vector<vk::SurfaceFormatKHR> f = p_device.getSurfaceFormatsKHR(surface);
	std::vector<vk::PresentModeKHR> pm = p_device.getSurfacePresentModesKHR(surface);
	return !f.empty() && !pm.empty();
}

bool PhysicalDevice::is_device_suitable(uint32_t idx, const vk::PhysicalDevice p_device, const DeviceFeatures& features, const std::optional<vk::SurfaceKHR>& surface)
{
	vk::PhysicalDeviceProperties pdp = p_device.getProperties();
	std::vector<vk::ExtensionProperties> available_extensions = p_device.enumerateDeviceExtensionProperties();
	std::vector<const char*> avail_ext_names;
	for (const vk::ExtensionProperties& ext : available_extensions) avail_ext_names.push_back(ext.extensionName);
	std::cerr << "  " << idx << " " << pdp.deviceName << " ";
	bool suitable = extensions_handler.check_extension_availability(avail_ext_names) && check_feature_support(p_device, features);
	if (suitable && surface.has_value() && extensions_handler.find_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
	{
		suitable = is_swapchain_supported(p_device, surface.value());
	}
	if (!suitable)
	{
		std::cerr << "(not suitable)\n";
		return false;
	}
	std::cerr << "(suitable)\n";
	return true;
}

bool PhysicalDevice::check_feature_support(vk::PhysicalDevice p_device, const DeviceFeatures& features) const
{
	const DeviceFeatureChain requested = build_required_feature_chain(features);
	DeviceFeatureChain supported;
	p_device.getFeatures2(&supported.get<vk::PhysicalDeviceFeatures2>());

	std::vector<std::string> missing;
	auto require = [&missing](vk::Bool32 is_requested, vk::Bool32 is_supported, const char* name) {
		if (is_requested && !is_supported) missing.push_back(name);
	};

	const vk::PhysicalDeviceFeatures& requested_core = requested.get<vk::PhysicalDeviceFeatures2>().features;
	const vk::PhysicalDeviceFeatures& supported_core = supported.get<vk::PhysicalDeviceFeatures2>().features;
	require(requested_core.samplerAnisotropy, supported_core.samplerAnisotropy, "samplerAnisotropy");
	require(requested_core.sampleRateShading, supported_core.sampleRateShading, "sampleRateShading");
	require(requested_core.fillModeNonSolid, supported_core.fillModeNonSolid, "fillModeNonSolid");
	require(requested_core.fragmentStoresAndAtomics, supported_core.fragmentStoresAndAtomics, "fragmentStoresAndAtomics");
	require(requested_core.wideLines, supported_core.wideLines, "wideLines");

	require(requested.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters, supported.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters, "shaderDrawParameters");

	const vk::PhysicalDeviceVulkan12Features& requested_12 = requested.get<vk::PhysicalDeviceVulkan12Features>();
	const vk::PhysicalDeviceVulkan12Features& supported_12 = supported.get<vk::PhysicalDeviceVulkan12Features>();
	require(requested_12.bufferDeviceAddress, supported_12.bufferDeviceAddress, "bufferDeviceAddress");
	require(requested_12.scalarBlockLayout, supported_12.scalarBlockLayout, "scalarBlockLayout");
	require(requested_12.descriptorBindingPartiallyBound, supported_12.descriptorBindingPartiallyBound, "descriptorBindingPartiallyBound");
	require(requested_12.shaderSampledImageArrayNonUniformIndexing, supported_12.shaderSampledImageArrayNonUniformIndexing, "shaderSampledImageArrayNonUniformIndexing");
	require(requested_12.runtimeDescriptorArray, supported_12.runtimeDescriptorArray, "runtimeDescriptorArray");

	const vk::PhysicalDeviceVulkan13Features& requested_13 = requested.get<vk::PhysicalDeviceVulkan13Features>();
	const vk::PhysicalDeviceVulkan13Features& supported_13 = supported.get<vk::PhysicalDeviceVulkan13Features>();
	require(requested_13.synchronization2, supported_13.synchronization2, "synchronization2");
	require(requested_13.shaderDemoteToHelperInvocation, supported_13.shaderDemoteToHelperInvocation, "shaderDemoteToHelperInvocation");
	require(requested_13.dynamicRendering, supported_13.dynamicRendering, "dynamicRendering");

	require(requested.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>().accelerationStructure, supported.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>().accelerationStructure, "accelerationStructure");
	require(requested.get<vk::PhysicalDeviceRayQueryFeaturesKHR>().rayQuery, supported.get<vk::PhysicalDeviceRayQueryFeaturesKHR>().rayQuery, "rayQuery");
	require(requested.get<vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT>().extendedDynamicState3PolygonMode, supported.get<vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT>().extendedDynamicState3PolygonMode, "extendedDynamicState3PolygonMode");

	const vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT& requested_af = requested.get<vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT>();
	const vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT& supported_af = supported.get<vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT>();
	require(requested_af.shaderBufferFloat32Atomics, supported_af.shaderBufferFloat32Atomics, "shaderBufferFloat32Atomics");
	require(requested_af.shaderBufferFloat32AtomicAdd, supported_af.shaderBufferFloat32AtomicAdd, "shaderBufferFloat32AtomicAdd");
	require(requested_af.shaderSharedFloat32Atomics, supported_af.shaderSharedFloat32Atomics, "shaderSharedFloat32Atomics");
	require(requested_af.shaderSharedFloat32AtomicAdd, supported_af.shaderSharedFloat32AtomicAdd, "shaderSharedFloat32AtomicAdd");
	require(requested_af.shaderImageFloat32Atomics, supported_af.shaderImageFloat32Atomics, "shaderImageFloat32Atomics");
	require(requested_af.shaderImageFloat32AtomicAdd, supported_af.shaderImageFloat32AtomicAdd, "shaderImageFloat32AtomicAdd");

	for (const std::string& name : missing) std::cerr << "\n    missing feature: " << name;
	return missing.empty();
}
} // namespace vkte
