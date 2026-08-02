#pragma once

#include "vkte/vulkan_main_context.hpp"
#include "vkte/storage.hpp"
#include <set>

namespace vkte
{
class AccelerationStructureBuilder
{
public:
	AccelerationStructureBuilder(const VulkanMainContext& vmc, Storage& storage);
	void destruct();
	void clean_up_scratch_buffers(bool keep_dynamic = true);
	struct BLASData
	{
		// id to the buffer in the storage class
		uint32_t vertex_buffer_id;
		uint32_t index_buffer_id;
		vk::DeviceSize vertex_stride;
		std::vector<uint32_t> index_offsets = {0};
		std::vector<uint32_t> index_counts = {};
		bool dynamic = false;
	};
	uint32_t add_blas(const std::string& buffer_name, const BLASData& blas_data);
	void update_blas(uint32_t blas_idx);
	uint32_t add_instance(uint32_t blas_idx, const vk::TransformMatrixKHR& M, uint32_t custom_index, uint8_t mask = 0xFF);
	void update_instance(uint32_t instance_idx, const vk::TransformMatrixKHR& M);
	void construct(vk::CommandBuffer& cb, QueueFamilyFlags build_queue, const std::string& buffer_name);
	void update_tlas(vk::CommandBuffer& cb, QueueFamilyFlags build_queue);

private:
	struct BLAS {
		std::vector<uint32_t> num_triangles;
		std::vector<vk::AccelerationStructureBuildRangeInfoKHR> asbris;
		std::vector<vk::AccelerationStructureGeometryKHR> asgs;
		vk::AccelerationStructureBuildGeometryInfoKHR asbgi;
		vk::AccelerationStructureCreateInfoKHR asci;
		vk::AccelerationStructureKHR handle;
		uint64_t device_address = 0;
		int32_t buffer = -1;
		int32_t scratch_buffer = -1;
		bool dynamic = false;
	};

	struct TLAS {
		vk::AccelerationStructureGeometryKHR asg;
		vk::AccelerationStructureBuildGeometryInfoKHR asbgi;
		uint32_t primitive_count;
		vk::AccelerationStructureCreateInfoKHR asci;
		vk::AccelerationStructureBuildRangeInfoKHR asbri;
		vk::AccelerationStructureKHR handle;
		uint64_t device_address = 0;
		int32_t buffer = -1;
		int32_t scratch_buffer = -1;
	};

	struct ScratchBuffer {
		uint32_t buffer;
		vk::DeviceAddress device_address;
	};

	ScratchBuffer create_scratch_buffer(const std::string& buffer_name, vk::DeviceSize build_scratch_size);

	const VulkanMainContext& vmc;
	Storage& storage;
	uint32_t scratch_offset_alignment = 0;
	vk::WriteDescriptorSetAccelerationStructureKHR wdsas;
	std::vector<BLAS> bottom_level_as;
	std::set<uint32_t> blas_update_indices;
	std::vector<vk::BufferMemoryBarrier> blas_memory_barriers;
	std::vector<vk::AccelerationStructureInstanceKHR> instances;
	int32_t instances_buffer = -1;
	TLAS top_level_as;
};
} // namespace vkte
