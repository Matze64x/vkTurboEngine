#pragma once

#include <array>
#include "vkte/global_constants.hpp"
#include "vkte/vulkan_main_context.hpp"
#include "vkte/memory_manager.hpp"
#include "vkte/resource_handles.hpp"
#include <set>

namespace vkte
{
class Engine;

// Changes are built when the next frame begins, after the previous frame's work and before the
// frame's own. Removed acceleration structures stay alive while a frame in flight can use them.
class AccelerationStructureBuilder
{
public:
	void destruct();
	struct BLASData
	{
		BufferHandle vertex_buffer_id;
		BufferHandle index_buffer_id;
		vk::DeviceSize vertex_stride;
		std::vector<uint32_t> index_offsets = {0};
		std::vector<uint32_t> index_counts = {};
		bool dynamic = false;
	};
	struct AABBBLASData
	{
		BufferHandle aabb_buffer_id;
		uint32_t aabb_offset = 0;
		uint32_t aabb_count = 1;
		bool dynamic = false;
	};
	BLASHandle add_blas(const std::string& buffer_name, const BLASData& blas_data);
	BLASHandle add_aabb_blas(const std::string& buffer_name, const AABBBLASData& blas_data);
	// rebuilds a dynamic BLAS from its buffers
	void update_blas(BLASHandle blas);
	void remove_blas(BLASHandle blas);
	InstanceHandle add_instance(BLASHandle blas, const vk::TransformMatrixKHR& M, uint32_t custom_index, uint8_t mask = 0xFF);
	void update_instance(InstanceHandle instance, const vk::TransformMatrixKHR& M);
	void set_instance_blas(InstanceHandle instance, BLASHandle blas);
	void remove_instance(InstanceHandle instance);
	BufferHandle get_tlas_buffer_handle() const { return tlas.buffer; }

private:
	struct BLAS {
		std::vector<uint32_t> primitive_counts;
		std::vector<vk::AccelerationStructureBuildRangeInfoKHR> asbris;
		std::vector<vk::AccelerationStructureGeometryKHR> asgs;
		vk::AccelerationStructureBuildGeometryInfoKHR asbgi;
		vk::AccelerationStructureCreateInfoKHR asci;
		vk::AccelerationStructureKHR handle;
		uint64_t device_address = 0;
		ResourceHandle buffer;
		ResourceHandle scratch_buffer;
		bool dynamic = false;
	};

	struct TLAS {
		vk::AccelerationStructureGeometryKHR asg;
		vk::AccelerationStructureBuildGeometryInfoKHR asbgi;
		uint32_t primitive_count = 0;
		vk::AccelerationStructureCreateInfoKHR asci;
		vk::AccelerationStructureBuildRangeInfoKHR asbri;
		vk::AccelerationStructureKHR handle;
		BufferHandle buffer;
		ResourceHandle scratch_buffer;
		vk::WriteDescriptorSetAccelerationStructureKHR wdsas;
	};

	struct ScratchBuffer {
		ResourceHandle buffer;
		vk::DeviceAddress device_address;
	};

	friend class Engine;
	AccelerationStructureBuilder(const VulkanMainContext& vmc, MemoryManager& memory_manager);
	bool has_frame_work() const;
	// Records the builds into cb.
	void begin_frame(vk::CommandBuffer& cb, uint32_t frame_index);

	ScratchBuffer create_scratch_buffer(const std::string& buffer_name, vk::DeviceSize build_scratch_size);
	BLASHandle create_blas(const std::string& buffer_name, BLAS blas);
	uint32_t get_index(BLASHandle blas) const;
	uint32_t get_index(InstanceHandle instance) const;
	void create_tlas();
	void destroy_tlas();
	vk::DeviceAddress write_instances(uint32_t frame_index);

	const VulkanMainContext& vmc;
	MemoryManager& memory_manager;
	uint32_t scratch_offset_alignment = 0;
	std::vector<BLAS> bottom_level_as;
	std::vector<uint32_t> blas_generations;
	std::vector<uint32_t> free_blas_indices;
	std::set<uint32_t> blas_update_indices;
	std::vector<vk::AccelerationStructureInstanceKHR> instances;
	std::vector<uint32_t> instance_generations;
	std::vector<uint32_t> free_instance_indices;
	// host visible, per frame in flight, since the previous frame's build may not have run yet
	std::array<ResourceHandle, frames_in_flight> instance_buffers;
	TLAS tlas;
	// every frame needs a TLAS, even without instances
	bool tlas_outdated = true;
};
} // namespace vkte
