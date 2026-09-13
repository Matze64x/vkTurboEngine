#include "vkte/logical_device.hpp"

#include "vkte/device_feature_chain.hpp"
#include "vkte/physical_device.hpp"

namespace vkte
{
void LogicalDevice::construct(const PhysicalDevice& p_device, const DeviceFeatures& features, const QueueFamilies& queue_families, std::unordered_map<QueueIndex, vk::Queue>& queues)
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

	DeviceFeatureChain feature_chain = build_required_feature_chain(features);

	vk::DeviceCreateInfo dci;
	dci.pNext = &feature_chain.get<vk::PhysicalDeviceFeatures2>();
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
