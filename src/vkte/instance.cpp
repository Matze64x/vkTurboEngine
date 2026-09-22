#include "vkte/instance.hpp"

#include "vkte/name_list.hpp"
#include "vkte/vkte_log.hpp"

namespace vkte
{
void Instance::construct(std::vector<const char*> required_extensions, std::vector<const char*> validation_layers, std::vector<vk::ValidationFeatureEnableEXT> validation_feature_enables)
{
	required_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	if (!validation_feature_enables.empty()) required_extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);

	vk::ApplicationInfo ai;
	ai.pApplicationName = "Vulkan Engine";
	ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	ai.pEngineName = "vkTurboEngine";
	ai.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	ai.apiVersion = VK_API_VERSION_1_4;

	std::vector<vk::LayerProperties> available_layers = vk::enumerateInstanceLayerProperties();
	std::vector<const char*> avail_layer_names;
	for (const vk::LayerProperties& layer : available_layers) avail_layer_names.push_back(layer.layerName);
	this->validation_layers = std::move(validation_layers);
	if (!all_available(this->validation_layers, avail_layer_names)) VKTE_THROW("vkte: Requested validation layer not found!");

	std::vector<vk::ExtensionProperties> available_extensions = vk::enumerateInstanceExtensionProperties();
	std::vector<const char*> avail_ext_names;
	for (const vk::ExtensionProperties& ext : available_extensions) avail_ext_names.push_back(ext.extensionName);
	std::vector<std::vector<vk::ExtensionProperties>> layer_extensions;
	for (const char* layer : this->validation_layers)
	{
		layer_extensions.push_back(vk::enumerateInstanceExtensionProperties(std::string(layer)));
		for (const vk::ExtensionProperties& ext : layer_extensions.back()) avail_ext_names.push_back(ext.extensionName);
	}
	extensions = std::move(required_extensions);
	if (!all_available(extensions, avail_ext_names)) VKTE_THROW("vkte: Requested instance extension not found!");

	vk::ValidationFeaturesEXT vfe;
	vfe.enabledValidationFeatureCount = validation_feature_enables.size();
	vfe.pEnabledValidationFeatures = validation_feature_enables.data();

	vk::InstanceCreateInfo ici;
	ici.pApplicationInfo = &ai;
	ici.enabledExtensionCount = extensions.size();
	ici.ppEnabledExtensionNames = extensions.data();
	ici.enabledLayerCount = this->validation_layers.size();
	ici.ppEnabledLayerNames = this->validation_layers.data();
	if (!validation_feature_enables.empty()) ici.pNext = &vfe;

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
