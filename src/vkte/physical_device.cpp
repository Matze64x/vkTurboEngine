#include "vkte/physical_device.hpp"

#include <iostream>
#include <string>
#include <unordered_set>

#include "vkte/device_feature_chain.hpp"
#include "vkte/name_list.hpp"
#include "vkte/vkte_log.hpp"

namespace vkte
{
void PhysicalDevice::construct(const Instance& instance, const std::vector<const char*>& required_extensions, const DeviceFeatures& features, const std::optional<vk::SurfaceKHR>& surface)
{
	extensions.insert(extensions.end(), required_extensions.begin(), required_extensions.end());

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
	return extensions;
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
	bool suitable = all_available(extensions, avail_ext_names) && check_feature_support(p_device, features);
	if (suitable && surface.has_value() && contains(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
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
	const DeviceFeatureChain supported = query_supported_feature_chain(p_device);
	return is_feature_chain_satisfied(requested, supported);
}
} // namespace vkte
