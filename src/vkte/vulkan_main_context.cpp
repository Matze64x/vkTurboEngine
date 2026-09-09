#include "vkte/vulkan_main_context.hpp"

#include "vkte/vkte_log.hpp"
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

static VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(vk::DebugUtilsMessageSeverityFlagBitsEXT message_severity, vk::DebugUtilsMessageTypeFlagsEXT message_type, const vk::DebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data)
{
	switch (message_severity)
	{
		case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
			VKTE_DEBUG("vkte: {}", callback_data->pMessage);
			break;
		case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
			VKTE_INFO("vkte: {}", callback_data->pMessage);
			break;
		case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
			VKTE_WARN("vkte: {}", callback_data->pMessage);
			break;
		case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
			VKTE_ERROR("vkte: {}", callback_data->pMessage);
			break;
	}
	return VK_FALSE;
}

namespace vkte
{
#if ENABLE_VKTE_WINDOW
void VulkanMainContext::construct(const std::string& title, const uint32_t width, const uint32_t height, const Features& features)
{
	this->features = features;
	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
	VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
	window.construct(title, width, height);
	window.hide();
	std::vector<const char*> instance_extensions = window.get_required_extensions();
	std::vector<const char*> validation_layers;
	if (features.khronos_validation) validation_layers.push_back("VK_LAYER_KHRONOS_validation");
	instance.construct(instance_extensions, validation_layers);
	VULKAN_HPP_DEFAULT_DISPATCHER.init(instance.get());
	surface = window.create_surface(instance.get());
	std::vector<const char*> device_extensions;
	if (features.swapchain) device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	if (features.device_features.acceleration_structure)
	{
		device_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
		device_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	}
	if (features.device_features.ray_query) device_extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
	if (features.device_features.dynamic_polygon_mode) device_extensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
	if (features.device_features.shader_atomic_float) device_extensions.push_back(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
	physical_device.construct(instance, device_extensions, surface);
	queue_families.construct(physical_device.get(), surface);
	logical_device.construct(physical_device, features.device_features, queue_families, queues);
	VULKAN_HPP_DEFAULT_DISPATCHER.init(logical_device.get());
	create_vma_allocator();
	setup_debug_messenger();
	window.show();
}
#else
void VulkanMainContext::construct(const Features& features)
{
	this->features = features;
	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
	VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
	std::vector<const char*> instance_extensions;
	std::vector<const char*> validation_layers;
	if (features.khronos_validation) validation_layers.push_back("VK_LAYER_KHRONOS_validation");
	instance.construct(instance_extensions, validation_layers);
	VULKAN_HPP_DEFAULT_DISPATCHER.init(instance.get());
	std::vector<const char*> device_extensions;
	if (features.swapchain) device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	if (features.device_features.acceleration_structure)
	{
		device_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
		device_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	}
	if (features.device_features.ray_query) device_extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
	if (features.device_features.dynamic_polygon_mode) device_extensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
	if (features.device_features.shader_atomic_float) device_extensions.push_back(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
	physical_device.construct(instance, device_extensions, std::nullopt);
	queue_families.construct(physical_device.get(), {});
	logical_device.construct(physical_device, features.device_features, queue_families, queues);
	VULKAN_HPP_DEFAULT_DISPATCHER.init(logical_device.get());
	create_vma_allocator();
	setup_debug_messenger();
}
#endif


void VulkanMainContext::destruct()
{
	vmaDestroyAllocator(va);
	instance.get().destroySurfaceKHR(surface);
	logical_device.destruct();
	instance.get().destroyDebugUtilsMessengerEXT(debug_messenger, nullptr, VULKAN_HPP_DEFAULT_DISPATCHER);
	instance.destruct();
#if ENABLE_VKTE_WINDOW
	window.destruct();
#endif
}

#if ENABLE_VKTE_WINDOW
std::vector<vk::SurfaceFormatKHR> VulkanMainContext::get_surface_formats() const
{
	return physical_device.get().getSurfaceFormatsKHR(surface);
}

std::vector<vk::PresentModeKHR> VulkanMainContext::get_surface_present_modes() const
{
	return physical_device.get().getSurfacePresentModesKHR(surface);
}

vk::SurfaceCapabilitiesKHR VulkanMainContext::get_surface_capabilities() const
{
	return physical_device.get().getSurfaceCapabilitiesKHR(surface);
}
#endif

const vk::Queue& VulkanMainContext::get_graphics_queue() const
{
	return queues.at(QueueIndex::Graphics);
}

const vk::Queue& VulkanMainContext::get_transfer_queue() const
{
	return queues.at(QueueIndex::Transfer);
}

const vk::Queue& VulkanMainContext::get_compute_queue() const
{
	return queues.at(QueueIndex::Compute);
}

const vk::Queue& VulkanMainContext::get_present_queue() const
{
	return queues.at(QueueIndex::Present);
}

const Features& VulkanMainContext::get_features() const
{
	return features;
}

void VulkanMainContext::create_vma_allocator()
{
	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
	PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = dl.getProcAddress<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
	VmaAllocatorCreateInfo vaci{};
	vaci.instance = instance.get();
	vaci.physicalDevice = physical_device.get();
	vaci.device = logical_device.get();
	vaci.vulkanApiVersion = VK_API_VERSION_1_3;
	vaci.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	VmaVulkanFunctions vvf{};
	vvf.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
	vvf.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
	vaci.pVulkanFunctions = &vvf;
	vmaCreateAllocator(&vaci, &va);
}

void VulkanMainContext::setup_debug_messenger()
{
	vk::DebugUtilsMessengerCreateInfoEXT dumci;
	dumci.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
	dumci.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
	dumci.pfnUserCallback = debug_callback;
	debug_messenger = instance.get().createDebugUtilsMessengerEXT(dumci, nullptr, VULKAN_HPP_DEFAULT_DISPATCHER);
}
} // namespace vkte
