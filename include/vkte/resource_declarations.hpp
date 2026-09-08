#pragma once

#include <memory>
#include <optional>
#include <vector>
#include "vulkan/vulkan.hpp"
#include "vkte/pipeline.hpp"
#include "vkte/resource_handles.hpp"

namespace vkte
{
class ResourceDeclarations
{
public:
	DescriptorSetLayoutHandle add_descriptor_set_layout();
	void add_binding(DescriptorSetLayoutHandle layout, uint32_t binding, vk::DescriptorType type, vk::ShaderStageFlags stages, uint32_t count = 1);
	DescriptorSetsHandle add_descriptor_sets(DescriptorSetLayoutHandle layout, uint32_t set_count = 1);
	void add_buffer_descriptor(DescriptorSetsHandle sets, uint32_t set, uint32_t binding, uint32_t buffer);
	void add_buffer_descriptor(DescriptorSetsHandle sets, uint32_t set, uint32_t binding, const std::vector<uint32_t>& buffers);
	void add_image_descriptor(DescriptorSetsHandle sets, uint32_t set, uint32_t binding, uint32_t image);
	void add_image_descriptor(DescriptorSetsHandle sets, uint32_t set, uint32_t binding, const std::vector<uint32_t>& images);
	PipelineHandle add_pipeline(const Pipeline::GraphicsSettings& settings, DescriptorSetLayoutHandle layout);
	PipelineHandle add_pipeline(const Pipeline::ComputeSettings& settings, DescriptorSetLayoutHandle layout);

private:
	friend class Engine;

	struct Descriptor
	{
		uint32_t set;
		uint32_t binding;
		bool is_image;
		vk::DescriptorType type;
		std::vector<uint32_t> resources;
	};

	struct LayoutEntry
	{
		explicit LayoutEntry(uint32_t owner) : owner(owner) {}
		std::vector<vk::DescriptorSetLayoutBinding> bindings;
		uint32_t owner;
	};

	struct SetsEntry
	{
		SetsEntry(DescriptorSetLayoutHandle layout, uint32_t set_count, uint32_t binding_count, uint32_t owner)
			: layout(layout), set_count(set_count), descriptors(binding_count * set_count), owner(owner)
		{}
		DescriptorSetLayoutHandle layout;
		uint32_t set_count;
		std::vector<std::optional<Descriptor>> descriptors;
		uint32_t owner;
	};

	struct PipelineEntry
	{
		PipelineEntry(const Pipeline::GraphicsSettings& settings, DescriptorSetLayoutHandle layout, uint32_t owner);
		PipelineEntry(const Pipeline::ComputeSettings& settings, DescriptorSetLayoutHandle layout, uint32_t owner);
		std::unique_ptr<Pipeline::GraphicsSettings> graphics_settings;
		std::unique_ptr<Pipeline::ComputeSettings> compute_settings;
		DescriptorSetLayoutHandle layout;
		uint32_t owner;
	};

	LayoutEntry& get_descriptor_set_layout(DescriptorSetLayoutHandle handle);
	SetsEntry& get_descriptor_sets(DescriptorSetsHandle handle);
	void add_descriptor(DescriptorSetsHandle sets, uint32_t set, uint32_t binding, bool is_image, const std::vector<uint32_t>& resources);

	std::vector<std::unique_ptr<LayoutEntry>> layouts;
	std::vector<std::unique_ptr<SetsEntry>> sets;
	std::vector<std::unique_ptr<PipelineEntry>> pipelines;
	// which component is declaring, so each entry records its owner; ~0u outside the phase
	uint32_t declaring_component = ~0u;
};
} // namespace vkte
