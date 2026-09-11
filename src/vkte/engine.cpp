#include "vkte/engine.hpp"

#include <algorithm>
#include <deque>
#include <unordered_map>
#include "vkte/vkte_log.hpp"

namespace vkte
{
Engine::Engine(const EngineSettings& settings) : vcc(vmc), storage(vmc, vcc), shader_repository(thread_manager)
{
#if ENABLE_VKTE_WINDOW
	vmc.construct(settings.window_title, settings.window_width, settings.window_height, settings.features);
#else
	vmc.construct(settings.features);
#endif
	vcc.construct();
	shader_repository.construct(vmc.logical_device.get(), settings.shader_root_dir);
#if ENABLE_VKTE_WINDOW
	if (settings.create_swapchain)
	{
		swapchain.construct(vmc, vcc, storage, settings.vsync);
		swapchain_constructed = true;
	}
	if (settings.create_ui)
	{
		VKTE_ASSERT(swapchain_constructed, "vkte: UI requires a swapchain; set EngineSettings::create_swapchain too.");
		ui.construct(vmc, swapchain);
		ui_constructed = true;
	}
#endif
}

Engine::~Engine()
{
#if ENABLE_VKTE_WINDOW
	if (ui_constructed) ui.destruct(vmc);
	if (swapchain_constructed) swapchain.destruct(vmc, storage);
#endif
	storage.clear();
	for (const vk::Semaphore& semaphore : semaphores)
	{
		if (semaphore) vmc.logical_device.get().destroySemaphore(semaphore);
	}
	for (const vk::Fence& fence : fences)
	{
		if (fence) vmc.logical_device.get().destroyFence(fence);
	}
	device_timers.clear();
	shader_repository.destruct();
	vcc.destruct();
	vmc.destruct();
}

void Engine::register_component(Component& component)
{
	components.push_back(&component);
}

const vk::Pipeline& Engine::get_pipeline(PipelineHandle handle) const
{
	VKTE_ASSERT(handle.valid() && handle.index < pipelines.size(), "vkte: Invalid pipeline handle!");
	return pipelines.at(handle.index).get();
}

const vk::PipelineLayout& Engine::get_pipeline_layout(PipelineHandle handle) const
{
	VKTE_ASSERT(handle.valid() && handle.index < pipelines.size(), "vkte: Invalid pipeline handle!");
	return pipelines.at(handle.index).get_layout();
}

const vk::DescriptorSetLayout& Engine::get_descriptor_set_layout(DescriptorSetLayoutHandle handle) const
{
	VKTE_ASSERT(handle.valid() && handle.index < descriptor_set_layouts.size(), "vkte: Invalid descriptor set layout handle!");
	return descriptor_set_layouts.at(handle.index);
}

const std::vector<vk::DescriptorSet>& Engine::get_descriptor_sets(DescriptorSetsHandle handle) const
{
	VKTE_ASSERT(handle.valid() && handle.index < descriptor_sets.size(), "vkte: Invalid descriptor sets handle!");
	return descriptor_sets.at(handle.index);
}

void Engine::construct_components(
#if ENABLE_VKTE_WINDOW
	const FrameSettings& settings
#endif
)
{
#if ENABLE_VKTE_WINDOW
	frame_settings = settings;
#endif
	for (uint32_t i = 0; i < components.size(); i++)
	{
		declarations.declaring_component = i;
		components[i]->declare_resources(declarations, storage, frame_settings);
	}
	declarations.declaring_component = ~0u;
	build_descriptor_set_layouts();

	pipelines.reserve(declarations.pipelines.size());
	std::vector<const Shader*> shaders;
	for (const std::unique_ptr<ResourceDeclarations::PipelineEntry>& entry : declarations.pipelines)
	{
		pipelines.emplace_back(vmc);
		if (entry->graphics_settings)
		{
			for (const Shader& shader : entry->graphics_settings->shaders) shaders.push_back(&shader);
		}
		else if (entry->compute_settings)
		{
			shaders.push_back(&entry->compute_settings->shader);
		}
		else VKTE_THROW("vkte: Pipeline with no valid settings!");
	}
	VKTE_ASSERT(shader_repository.compile_all(shaders), "vkte: Failed to compile one or more declared shaders!");

	build_pipelines();
	build_descriptor_sets();
}

void Engine::build_descriptor_set_layouts()
{
	descriptor_set_layouts.reserve(declarations.layouts.size());
	for (uint32_t i = 0; i < declarations.layouts.size(); i++)
	{
		ResourceDeclarations::LayoutEntry& entry = *declarations.layouts[i];
		std::sort(entry.bindings.begin(), entry.bindings.end(), [](const vk::DescriptorSetLayoutBinding& a, const vk::DescriptorSetLayoutBinding& b) { return a.binding < b.binding; });

		std::vector<vk::DescriptorBindingFlags> binding_flags(entry.bindings.size(), vk::DescriptorBindingFlagBits::ePartiallyBound);
		vk::DescriptorSetLayoutBindingFlagsCreateInfo dslbfci;
		dslbfci.bindingCount = binding_flags.size();
		dslbfci.pBindingFlags = binding_flags.data();

		vk::DescriptorSetLayoutCreateInfo dslci;
		dslci.bindingCount = entry.bindings.size();
		dslci.pBindings = entry.bindings.data();
		if (!entry.bindings.empty()) dslci.pNext = &dslbfci;

		vk::DescriptorSetLayout layout = vmc.logical_device.get().createDescriptorSetLayout(dslci);
		set_debug_name(vk::ObjectType::eDescriptorSetLayout, uint64_t(static_cast<vk::DescriptorSetLayout::CType>(layout)), std::format("{}_set_layout_{}", owner_name(entry.owner), i));
		descriptor_set_layouts.push_back(layout);
	}
}

void Engine::build_pipelines()
{
	for (uint32_t i = 0; i < declarations.pipelines.size(); i++)
	{
		const ResourceDeclarations::PipelineEntry& entry = *declarations.pipelines[i];
		Pipeline& pipeline = pipelines[i];
		vk::DescriptorSetLayout* set_layout = entry.layout.valid() ? &descriptor_set_layouts.at(entry.layout.index) : nullptr;
		if (entry.graphics_settings) pipeline.construct(*entry.graphics_settings, shader_repository, set_layout);
		else if (entry.compute_settings) pipeline.construct(*entry.compute_settings, shader_repository, set_layout);
		else VKTE_THROW("vkte: Pipeline with no valid settings!");
		set_debug_name(vk::ObjectType::ePipeline, uint64_t(static_cast<vk::Pipeline::CType>(pipeline.get())), std::format("{}_pipeline_{}", owner_name(entry.owner), i));
	}
}

void Engine::build_descriptor_sets()
{
	uint32_t total_sets = 0;
	std::unordered_map<vk::DescriptorType, uint32_t> descriptor_counts;
	for (const std::unique_ptr<ResourceDeclarations::SetsEntry>& entry : declarations.sets)
	{
		total_sets += entry->set_count;
		for (const vk::DescriptorSetLayoutBinding& binding : declarations.layouts.at(entry->layout.index)->bindings)
		{
			descriptor_counts[binding.descriptorType] += binding.descriptorCount * entry->set_count;
		}
	}
	if (total_sets == 0) return;

	std::vector<vk::DescriptorPoolSize> pool_sizes;
	for (const std::pair<const vk::DescriptorType, uint32_t>& count : descriptor_counts) pool_sizes.emplace_back(count.first, count.second);
	vk::DescriptorPoolCreateInfo dpci;
	dpci.poolSizeCount = pool_sizes.size();
	dpci.pPoolSizes = pool_sizes.data();
	dpci.maxSets = total_sets;
	descriptor_pool = vmc.logical_device.get().createDescriptorPool(dpci);

	descriptor_sets.resize(declarations.sets.size());
	for (uint32_t i = 0; i < declarations.sets.size(); i++)
	{
		const ResourceDeclarations::SetsEntry& entry = *declarations.sets[i];
		// all the same layout, to allocate every set of this declaration at once
		std::vector<vk::DescriptorSetLayout> layouts(entry.set_count, descriptor_set_layouts.at(entry.layout.index));
		vk::DescriptorSetAllocateInfo dsai;
		dsai.descriptorPool = descriptor_pool;
		dsai.descriptorSetCount = layouts.size();
		dsai.pSetLayouts = layouts.data();
		descriptor_sets[i] = vmc.logical_device.get().allocateDescriptorSets(dsai);
	}

	// deques, because the infos have to stay put until the single updateDescriptorSets() below reads them
	std::deque<std::vector<vk::DescriptorBufferInfo>> buffer_infos;
	std::deque<std::vector<vk::DescriptorImageInfo>> image_infos;
	std::vector<vk::WriteDescriptorSet> writes;
	for (uint32_t i = 0; i < declarations.sets.size(); i++)
	{
		const ResourceDeclarations::SetsEntry& entry = *declarations.sets[i];
		for (const std::optional<ResourceDeclarations::Descriptor>& slot : entry.descriptors)
		{
			// a binding that nothing was ever written to (relying on the partially-bound flag) leaves its slot empty
			if (!slot) continue;
			const ResourceDeclarations::Descriptor& descriptor = slot.value();

			vk::WriteDescriptorSet wds;
			wds.dstSet = descriptor_sets[i][descriptor.set];
			wds.dstBinding = descriptor.binding;
			wds.dstArrayElement = 0;
			wds.descriptorType = descriptor.type;
			if (!descriptor.resources.front().is_image)
			{
				std::vector<vk::DescriptorBufferInfo>& infos = buffer_infos.emplace_back();
				for (const ResourceHandle& resource : descriptor.resources)
				{
					Buffer& buffer = storage.get_buffer(resource);
					infos.emplace_back(buffer.get(), 0, buffer.get_byte_size());
					if (buffer.pNext) wds.pNext = buffer.pNext;
				}
				wds.pBufferInfo = infos.data();
				wds.descriptorCount = infos.size();
			}
			else
			{
				std::vector<vk::DescriptorImageInfo>& infos = image_infos.emplace_back();
				for (const ResourceHandle& resource : descriptor.resources)
				{
					Image& image = storage.get_image(resource);
					infos.emplace_back(image.get_sampler(), image.get_view(), image.get_layout());
				}
				wds.pImageInfo = infos.data();
				wds.descriptorCount = infos.size();
			}
			if (wds.descriptorCount > 0) writes.push_back(wds);
		}
	}
	vmc.logical_device.get().updateDescriptorSets(writes, {});
}

void Engine::wait_idle() const
{
	vmc.logical_device.get().waitIdle();
}

uint32_t Engine::get_queue_family_index(QueueFamilyFlags queue) const
{
	return vmc.queue_families.get(queue);
}

#if ENABLE_VKTE_WINDOW
void Engine::resize(bool vsync)
{
	VKTE_ASSERT(swapchain_constructed, "vkte: Trying to resize a swapchain that was never constructed! Set EngineSettings::create_swapchain.");
	swapchain.destruct(vmc, storage);
	swapchain.construct(vmc, vcc, storage, vsync);
}
#endif

SemaphoreHandle Engine::add_semaphore()
{
	vk::SemaphoreCreateInfo sci;
	semaphores.push_back(vmc.logical_device.get().createSemaphore(sci));
	return SemaphoreHandle{uint32_t(semaphores.size() - 1)};
}

void Engine::destroy(SemaphoreHandle handle)
{
	VKTE_ASSERT(handle.valid() && handle.index < semaphores.size(), "vkte: Invalid semaphore handle!");
	vmc.logical_device.get().destroySemaphore(semaphores.at(handle.index));
	semaphores.at(handle.index) = vk::Semaphore();
}

vk::Semaphore Engine::get(SemaphoreHandle handle) const
{
	VKTE_ASSERT(handle.valid() && handle.index < semaphores.size(), "vkte: Invalid semaphore handle!");
	return semaphores.at(handle.index);
}

FenceHandle Engine::add_fence()
{
	// all fences are created as signaled, if an unsignaled fence is needed use reset_fence
	vk::FenceCreateInfo fci;
	fci.flags = vk::FenceCreateFlagBits::eSignaled;
	fences.push_back(vmc.logical_device.get().createFence(fci));
	return FenceHandle{uint32_t(fences.size() - 1)};
}

void Engine::destroy(FenceHandle handle)
{
	VKTE_ASSERT(handle.valid() && handle.index < fences.size(), "vkte: Invalid fence handle!");
	vmc.logical_device.get().destroyFence(fences.at(handle.index));
	fences.at(handle.index) = vk::Fence();
}

vk::Fence Engine::get(FenceHandle handle) const
{
	VKTE_ASSERT(handle.valid() && handle.index < fences.size(), "vkte: Invalid fence handle!");
	return fences.at(handle.index);
}

void Engine::wait_for_fence(FenceHandle handle) const
{
	VKTE_CHECK(vmc.logical_device.get().waitForFences(get(handle), 1, uint64_t(-1)), "vkte: Failed to wait for fence!");
}

void Engine::reset_fence(FenceHandle handle) const
{
	vmc.logical_device.get().resetFences(get(handle));
}

bool Engine::is_fence_finished(FenceHandle handle) const
{
	return vmc.logical_device.get().getFenceStatus(get(handle)) == vk::Result::eSuccess;
}

DeviceTimerHandle Engine::add_device_timer(uint32_t timer_count)
{
	device_timers.push_back(std::unique_ptr<DeviceTimer>(new DeviceTimer(vmc, timer_count)));
	return DeviceTimerHandle{uint32_t(device_timers.size() - 1)};
}

void Engine::destroy(DeviceTimerHandle handle)
{
	VKTE_ASSERT(handle.valid() && handle.index < device_timers.size() && device_timers.at(handle.index), "vkte: Invalid device timer handle!");
	device_timers.at(handle.index).reset();
}

DeviceTimer& Engine::get(DeviceTimerHandle handle) const
{
	VKTE_ASSERT(handle.valid() && handle.index < device_timers.size() && device_timers.at(handle.index), "vkte: Invalid device timer handle!");
	return *device_timers.at(handle.index);
}

AccelerationStructureBuilderHandle Engine::add_acceleration_structure_builder()
{
	acceleration_structure_builders.push_back(std::unique_ptr<AccelerationStructureBuilder>(new AccelerationStructureBuilder(vmc, storage)));
	return AccelerationStructureBuilderHandle{uint32_t(acceleration_structure_builders.size() - 1)};
}

void Engine::destroy(AccelerationStructureBuilderHandle handle)
{
	VKTE_ASSERT(handle.valid() && handle.index < acceleration_structure_builders.size() && acceleration_structure_builders.at(handle.index), "vkte: Invalid acceleration structure builder handle!");
	acceleration_structure_builders.at(handle.index).reset();
}

AccelerationStructureBuilder& Engine::get(AccelerationStructureBuilderHandle handle) const
{
	VKTE_ASSERT(handle.valid() && handle.index < acceleration_structure_builders.size() && acceleration_structure_builders.at(handle.index), "vkte: Invalid acceleration structure builder handle!");
	return *acceleration_structure_builders.at(handle.index);
}

const char* Engine::owner_name(uint32_t component) const
{
	if (component >= components.size()) return "engine";
	return components[component]->name();
}

bool Engine::reload_shaders_all()
{
	if (!shader_repository.recompile_all()) return false;
	for (Pipeline& pipeline : pipelines) pipeline.destruct();
	build_pipelines();
	return true;
}

void Engine::destruct_components()
{
	for (Pipeline& pipeline : pipelines) pipeline.destruct();
	pipelines.clear();
	declarations.pipelines.clear();
	if (descriptor_pool)
	{
		vmc.logical_device.get().destroyDescriptorPool(descriptor_pool);
		descriptor_pool = nullptr;
	}
	descriptor_sets.clear();
	declarations.sets.clear();
	for (const vk::DescriptorSetLayout& layout : descriptor_set_layouts) vmc.logical_device.get().destroyDescriptorSetLayout(layout);
	descriptor_set_layouts.clear();
	declarations.layouts.clear();
	for (Component* component : components) component->destruct(storage);
}

void Engine::set_debug_name(vk::ObjectType type, uint64_t handle, const std::string& name) const
{
	vk::DebugUtilsObjectNameInfoEXT duoni(type, handle, name.c_str());
	vmc.logical_device.get().setDebugUtilsObjectNameEXT(duoni);
}
} // namespace vkte
