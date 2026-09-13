#include "vkte/instance.hpp"

#include "vkte/name_list.hpp"
#include "vkte/vkte_log.hpp"

namespace vkte
{
void Instance::construct(std::vector<const char*> required_extensions, std::vector<const char*> validation_layers)
{
	required_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

	vk::ApplicationInfo ai;
	ai.pApplicationName = "Vulkan Engine";
	ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	ai.pEngineName = "vkTurboEngine";
	ai.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	ai.apiVersion = VK_API_VERSION_1_4;

	std::vector<vk::ExtensionProperties> available_extensions = vk::enumerateInstanceExtensionProperties();
	std::vector<const char*> avail_ext_names;
	for (const vk::ExtensionProperties& ext : available_extensions) avail_ext_names.push_back(ext.extensionName);
	extensions = std::move(required_extensions);
	if (!all_available(extensions, avail_ext_names)) VKTE_THROW("vkte: Requested instance extension not found!");

	std::vector<vk::LayerProperties> available_layers = vk::enumerateInstanceLayerProperties();
	std::vector<const char*> avail_layer_names;
	for (const auto& layer : available_layers) avail_layer_names.push_back(layer.layerName);
	this->validation_layers = std::move(validation_layers);
	if (!all_available(this->validation_layers, avail_layer_names)) VKTE_THROW("vkte: Requested validation layer not found!");

	vk::InstanceCreateInfo ici;
	ici.pApplicationInfo = &ai;
	ici.enabledExtensionCount = extensions.size();
	ici.ppEnabledExtensionNames = extensions.data();
	ici.enabledLayerCount = this->validation_layers.size();
	ici.ppEnabledLayerNames = this->validation_layers.data();

	instance = vk::createInstance(ici);
}

void Instance::destruct()
{
	instance.destroy();
}

const vk::Instance& Instance::get() const
{
	return instance;
}

std::vector<vk::PhysicalDevice> Instance::get_physical_devices() const
{
	std::vector<vk::PhysicalDevice> physical_devices = instance.enumeratePhysicalDevices();
	if (physical_devices.empty()) VKTE_THROW("vkte: Failed to find GPUs with Vulkan support!");
	return physical_devices;
}
} // namespace vkte
