#include "vkte/queue_families.hpp"
#include "vkte/vkte_log.hpp"

namespace vkte
{
void QueueFamilies::construct(vk::PhysicalDevice physical_device)
{
	get_queue_families(physical_device, {});
}

void QueueFamilies::construct(vk::PhysicalDevice physical_device, vk::SurfaceKHR surface)
{
	get_queue_families(physical_device, std::make_optional(surface));
}

std::vector<uint32_t> QueueFamilies::get(Queues queues) const
{
	std::vector<uint32_t> queue_indices;
	if ((queues & QueueFamilyFlags::Graphics) && indices.graphics != -1)
	{
		queue_indices.push_back(indices.graphics);
		queues &= ~graphics;
	}
	if ((queues & QueueFamilyFlags::Compute) && indices.compute != -1)
	{
		queue_indices.push_back(indices.compute);
		queues &= ~compute;
	}
	if ((queues & QueueFamilyFlags::Transfer) && indices.transfer != -1)
	{
		queue_indices.push_back(indices.transfer);
		queues &= ~transfer;
	}
	if ((queues & QueueFamilyFlags::Present) && indices.present != -1)
	{
		queue_indices.push_back(indices.present);
		queues &= ~present;
	}
	return queue_indices;
}

uint32_t QueueFamilies::get(QueueFamilyFlags queue) const
{
	if (queue == QueueFamilyFlags::Graphics) return indices.graphics;
	else if (queue == QueueFamilyFlags::Compute) return indices.compute;
	else if (queue == QueueFamilyFlags::Transfer) return indices.transfer;
	else if (queue == QueueFamilyFlags::Present) return indices.present;
	else VKTE_THROW("vkte: Invalid queue!");
}

int32_t get_queue_score(vk::QueueFamilyProperties queue_family, vk::QueueFlagBits target)
{
	// required queue family not supported by this queue
	if (!(queue_family.queueFlags & target)) return 0;
	int32_t score = 1;
	// every missing queue feature increases score, as this means that the queue is more specialized
	if (!(queue_family.queueFlags & vk::QueueFlagBits::eGraphics)) ++score;
	if (!(queue_family.queueFlags & vk::QueueFlagBits::eCompute)) ++score;
	if (!(queue_family.queueFlags & vk::QueueFlagBits::eProtected)) ++score;
	if (!(queue_family.queueFlags & vk::QueueFlagBits::eTransfer)) ++score;
	if (!(queue_family.queueFlags & vk::QueueFlagBits::eSparseBinding)) ++score;
	return score;
}

void QueueFamilies::get_queue_families(vk::PhysicalDevice& physical_device, const std::optional<vk::SurfaceKHR>& surface)
{
	std::vector<vk::QueueFamilyProperties> queue_families = physical_device.getQueueFamilyProperties();
	// use scores to determine how good a queue family fits for a task
	Indices scores(0);
	for (uint32_t i = 0; i < queue_families.size(); ++i)
	{
		vk::Bool32 present_support = false;
		if (surface.has_value())
		{
			present_support = physical_device.getSurfaceSupportKHR(i, surface.value());
			// take what we get for present queue, but ideally present and graphics queue are the same
			if (present_support && scores.present == 0)
			{
				scores.present = 1;
				indices.present = i;
			}
		}
		if (scores.graphics < get_queue_score(queue_families[i], vk::QueueFlagBits::eGraphics))
		{
			if (present_support && scores.present < 2)
			{
				scores.present = 2;
				indices.present = i;
			}
			scores.graphics = get_queue_score(queue_families[i], vk::QueueFlagBits::eGraphics);
			indices.graphics = i;
		}
		if (scores.compute < get_queue_score(queue_families[i], vk::QueueFlagBits::eCompute))
		{
			scores.compute = get_queue_score(queue_families[i], vk::QueueFlagBits::eCompute);
			indices.compute = i;
		}
		if (scores.transfer < get_queue_score(queue_families[i], vk::QueueFlagBits::eTransfer))
		{
			scores.transfer = get_queue_score(queue_families[i], vk::QueueFlagBits::eTransfer);
			indices.transfer = i;
		}
	}
	VKTE_ASSERT(indices.graphics != -1 && indices.compute != -1 && indices.transfer != -1 && (!surface.has_value() || indices.present != -1), "vkte: One queue family could not be satisfied!");
	if (indices.graphics == indices.compute)
	{
		graphics |= compute;
		compute |= graphics;
	}
	if (indices.graphics == indices.transfer)
	{
		graphics |= transfer;
		transfer |= graphics;
	}
	if (indices.graphics == indices.present)
	{
		graphics |= present;
		present |= graphics;
	}
	if (indices.compute == indices.transfer)
	{
		compute |= transfer;
		transfer |= compute;
	}
	if (indices.compute == indices.present)
	{
		compute |= present;
		present |= compute;
	}
	if (indices.transfer == indices.present)
	{
		transfer |= present;
		present |= transfer;
	}
}
} // namespace vkte
