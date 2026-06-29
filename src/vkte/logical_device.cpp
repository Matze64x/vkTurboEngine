#include "vkte/logical_device.hpp"

#include "vkte/physical_device.hpp"

namespace vkte
{
void LogicalDevice::construct(const PhysicalDevice& p_device, const Features& features, const QueueFamilies& queue_families, std::unordered_map<QueueIndex, vk::Queue>& queues)
{
	std::vector<vk::DeviceQueueCreateInfo> qci_s;
	std::vector<uint32_t> queue_indices = queue_families.get(QueueFamilyFlags::Graphics | QueueFamilyFlags::Compute | QueueFamilyFlags::Transfer | QueueFamilyFlags::Present);
	float queue_prio = 1.0f;
	for (uint32_t queue_family : queue_indices)
	{
		vk::DeviceQueueCreateInfo qci;
		qci.queueFamilyIndex = queue_family;
		qci.queueCount = 1;
		qci.pQueuePriorities = &queue_prio;
		qci_s.push_back(qci);
	}

	vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT atomic_float_features;
	atomic_float_features.shaderBufferFloat32Atomics = features.shader_atomic_float ? VK_TRUE : VK_FALSE;
	atomic_float_features.shaderBufferFloat32AtomicAdd = features.shader_atomic_float ? VK_TRUE : VK_FALSE;
	atomic_float_features.shaderSharedFloat32Atomics = features.shader_atomic_float ? VK_TRUE : VK_FALSE;
	atomic_float_features.shaderSharedFloat32AtomicAdd = features.shader_atomic_float ? VK_TRUE : VK_FALSE;
	atomic_float_features.shaderImageFloat32Atomics = features.shader_atomic_float ? VK_TRUE : VK_FALSE;
	atomic_float_features.shaderImageFloat32AtomicAdd = features.shader_atomic_float ? VK_TRUE : VK_FALSE;

	vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT dynamic_state_3;
	dynamic_state_3.extendedDynamicState3PolygonMode = features.dynamic_polygon_mode ? VK_TRUE : VK_FALSE;
	dynamic_state_3.pNext = &atomic_float_features;

	vk::PhysicalDeviceRayQueryFeaturesKHR rq_features;
	rq_features.pNext = &dynamic_state_3;
	rq_features.rayQuery = features.ray_query ? VK_TRUE : VK_FALSE;

	vk::PhysicalDeviceAccelerationStructureFeaturesKHR as_features;
	as_features.pNext = &rq_features;
	as_features.accelerationStructure = features.acceleration_structure ? VK_TRUE : VK_FALSE;

	vk::PhysicalDeviceVulkan11Features device_features_11;
	device_features_11.pNext = &as_features;
	device_features_11.shaderDrawParameters = VK_TRUE;

	vk::PhysicalDeviceVulkan12Features device_features_12;
	device_features_12.pNext = &device_features_11;
	device_features_12.bufferDeviceAddress = VK_TRUE;
	device_features_12.scalarBlockLayout = VK_TRUE;
	device_features_12.descriptorBindingPartiallyBound = VK_TRUE;
	device_features_12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	device_features_12.runtimeDescriptorArray = VK_TRUE;

	vk::PhysicalDeviceVulkan13Features device_features_13;
	device_features_13.pNext = &device_features_12;
	device_features_13.synchronization2 = VK_TRUE;
	device_features_13.shaderDemoteToHelperInvocation = VK_TRUE;
	device_features_13.dynamicRendering = VK_TRUE;

	vk::PhysicalDeviceFeatures core_device_features;
	core_device_features.samplerAnisotropy = VK_TRUE;
	core_device_features.sampleRateShading = VK_TRUE;
	core_device_features.fillModeNonSolid = VK_TRUE;
	core_device_features.fragmentStoresAndAtomics = VK_TRUE;
	core_device_features.wideLines = VK_TRUE;

	vk::PhysicalDeviceFeatures2 device_features;
	device_features.pNext = &device_features_13;
	device_features.features = core_device_features;

	vk::DeviceCreateInfo dci;
	dci.pNext = &device_features;
	dci.queueCreateInfoCount = qci_s.size();
	dci.pQueueCreateInfos = qci_s.data();
	dci.enabledExtensionCount = p_device.get_extensions().size();
	dci.ppEnabledExtensionNames = p_device.get_extensions().data();

	device = p_device.get().createDevice(dci);
	std::unordered_map<uint32_t, std::string> queue_names;
	{
		uint32_t idx = queue_families.get(QueueFamilyFlags::Graphics);
		queues.emplace(QueueIndex::Graphics, device.getQueue(idx, 0));
		if (queue_names.contains(idx)) queue_names.at(idx) += ", graphics";
		else queue_names.emplace(idx, "graphics");
	}
	{
		uint32_t idx = queue_families.get(QueueFamilyFlags::Compute);
		queues.emplace(QueueIndex::Compute, device.getQueue(idx, 0));
		if (queue_names.contains(idx)) queue_names.at(idx) += ", compute";
		else queue_names.emplace(idx, "compute");
	}
	{
		uint32_t idx = queue_families.get(QueueFamilyFlags::Transfer);
		queues.emplace(QueueIndex::Transfer, device.getQueue(idx, 0));
		if (queue_names.contains(idx)) queue_names.at(idx) += ", transfer";
		else queue_names.emplace(idx, "transfer");
	}
	if (queue_families.get(QueueFamilyFlags::Present) != -1)
	{
		uint32_t idx = queue_families.get(QueueFamilyFlags::Present);
		queues.emplace(QueueIndex::Present, device.getQueue(idx, 0));
		if (queue_names.contains(idx)) queue_names.at(idx) += ", present";
		else queue_names.emplace(idx, "present");
	}
	for (const std::pair<uint32_t, std::string>& queue_name : queue_names)
	{
		vk::DebugUtilsObjectNameInfoEXT duoni(vk::Queue::objectType, uint64_t(static_cast<vk::Queue::CType>(device.getQueue(queue_name.first, 0))), queue_name.second.c_str());
		device.setDebugUtilsObjectNameEXT(duoni);
	}
}

void LogicalDevice::destruct()
{
	device.destroy();
}

const vk::Device& LogicalDevice::get() const
{
	return device;
}
} // namespace vkte
