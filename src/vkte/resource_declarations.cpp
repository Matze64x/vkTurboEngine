#include "vkte/resource_declarations.hpp"
#include "vkte/vkte_log.hpp"

namespace vkte
{
DescriptorSetLayoutHandle ResourceDeclarations::add_descriptor_set_layout()
{
	layouts.push_back(std::make_unique<LayoutEntry>(declaring_component));
	return DescriptorSetLayoutHandle{uint32_t(layouts.size() - 1)};
}

void ResourceDeclarations::add_binding(DescriptorSetLayoutHandle layout, uint32_t binding, vk::DescriptorType type, vk::ShaderStageFlags stages, uint32_t count)
{
	LayoutEntry& entry = get_descriptor_set_layout(layout);
	for (const vk::DescriptorSetLayoutBinding& existing : entry.bindings)
	{
		VKTE_ASSERT(existing.binding != binding, "vkte: Binding added to a descriptor set layout twice!");
	}
	// Vulkan does not allow a descriptor count of 0; a binding nothing is written to relies on the partially-bound flag the Engine sets
	entry.bindings.push_back(vk::DescriptorSetLayoutBinding{binding, type, std::max(count, 1u), stages});
}

DescriptorSetsHandle ResourceDeclarations::add_descriptor_sets(DescriptorSetLayoutHandle layout, uint32_t set_count)
{
	const LayoutEntry& layout_entry = get_descriptor_set_layout(layout);
	VKTE_ASSERT(set_count > 0, "vkte: Descriptor sets declared with a set count of 0!");
	VKTE_ASSERT(!layout_entry.bindings.empty(), "vkte: Descriptor sets declared for a layout with no bindings!");
	sets.push_back(std::make_unique<SetsEntry>(layout, set_count, uint32_t(layout_entry.bindings.size()), declaring_component));
	return DescriptorSetsHandle{uint32_t(sets.size() - 1)};
}

void ResourceDeclarations::add_descriptor(DescriptorSetsHandle sets, uint32_t set, uint32_t binding, ResourceHandle resource)
{
	add_descriptor(sets, set, binding, std::vector<ResourceHandle>{std::move(resource)});
}

PipelineHandle ResourceDeclarations::add_pipeline(const Pipeline::GraphicsSettings& settings, DescriptorSetLayoutHandle layout)
{
	pipelines.push_back(std::make_unique<PipelineEntry>(settings, layout, declaring_component));
	return PipelineHandle{uint32_t(pipelines.size() - 1)};
}

PipelineHandle ResourceDeclarations::add_pipeline(const Pipeline::ComputeSettings& settings, DescriptorSetLayoutHandle layout)
{
	pipelines.push_back(std::make_unique<PipelineEntry>(settings, layout, declaring_component));
	return PipelineHandle{uint32_t(pipelines.size() - 1)};
}

void ResourceDeclarations::add_descriptor(DescriptorSetsHandle handle, uint32_t set, uint32_t binding, std::vector<ResourceHandle> resources)
{
	VKTE_ASSERT(!resources.empty(), "vkte: Descriptor added with no resources!");
	const bool is_image = resources.front().is_image;
	for (const ResourceHandle& resource : resources) VKTE_ASSERT(resource.is_image == is_image, "vkte: Descriptor resources must all be the same kind (buffer or image)!");

	SetsEntry& entry = get_descriptor_sets(handle);
	VKTE_ASSERT(set < entry.set_count, "vkte: Descriptor added to a set beyond the declared set count!");

	const LayoutEntry& layout_entry = get_descriptor_set_layout(entry.layout);
	uint32_t binding_index = ~0u;
	for (uint32_t i = 0; i < layout_entry.bindings.size(); i++)
	{
		if (layout_entry.bindings[i].binding == binding)
		{
			binding_index = i;
			break;
		}
	}
	VKTE_ASSERT(binding_index != ~0u, "vkte: Descriptor added for a binding that was not declared on the layout!");

	std::optional<Descriptor>& slot = entry.descriptors[binding_index * entry.set_count + set];
	VKTE_ASSERT(!slot.has_value(), "vkte: Descriptor already added for this binding and set!");
	slot = Descriptor{set, binding, layout_entry.bindings[binding_index].descriptorType, std::move(resources)};
}

ResourceDeclarations::LayoutEntry& ResourceDeclarations::get_descriptor_set_layout(DescriptorSetLayoutHandle handle)
{
	VKTE_ASSERT(handle.valid() && handle.index < layouts.size(), "vkte: Invalid descriptor set layout handle!");
	return *layouts[handle.index];
}

ResourceDeclarations::SetsEntry& ResourceDeclarations::get_descriptor_sets(DescriptorSetsHandle handle)
{
	VKTE_ASSERT(handle.valid() && handle.index < sets.size(), "vkte: Invalid descriptor sets handle!");
	return *sets[handle.index];
}

ResourceDeclarations::PipelineEntry::PipelineEntry(const Pipeline::GraphicsSettings& settings, DescriptorSetLayoutHandle layout, uint32_t owner)
	: graphics_settings(std::make_unique<Pipeline::GraphicsSettings>(settings)), layout(layout), owner(owner)
{}

ResourceDeclarations::PipelineEntry::PipelineEntry(const Pipeline::ComputeSettings& settings, DescriptorSetLayoutHandle layout, uint32_t owner)
	: compute_settings(std::make_unique<Pipeline::ComputeSettings>(settings)), layout(layout), owner(owner)
{}
} // namespace vkte
