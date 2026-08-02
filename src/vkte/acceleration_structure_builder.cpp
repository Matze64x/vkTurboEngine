#include "vkte/acceleration_structure_builder.hpp"

namespace vkte
{
AccelerationStructureBuilder::AccelerationStructureBuilder(const VulkanMainContext& vmc, Storage& storage) : vmc(vmc), storage(storage) {}

AccelerationStructureBuilder::ScratchBuffer AccelerationStructureBuilder::create_scratch_buffer(const std::string& buffer_name, vk::DeviceSize build_scratch_size)
{
	if (scratch_offset_alignment == 0)
	{
		vk::PhysicalDeviceAccelerationStructurePropertiesKHR as_properties;
		vk::PhysicalDeviceProperties2 properties;
		properties.pNext = &as_properties;
		vmc.physical_device.get().getProperties2(&properties);
		scratch_offset_alignment = as_properties.minAccelerationStructureScratchOffsetAlignment;
	}

	// Pad buffer and round address up to requirement.
	const uint32_t buffer_idx = storage.add_buffer(buffer_name + " scratch (vkte internal)", build_scratch_size + scratch_offset_alignment - 1, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, true, QueueFamilyFlags::Compute | QueueFamilyFlags::Graphics | QueueFamilyFlags::Transfer);
	const vk::DeviceAddress device_address = storage.get_buffer(buffer_idx).get_device_address();
	const vk::DeviceSize misalignment = device_address % scratch_offset_alignment;
	return ScratchBuffer{buffer_idx, misalignment == 0 ? device_address : device_address + (scratch_offset_alignment - misalignment)};
}

void AccelerationStructureBuilder::destruct()
{
	vmc.logical_device.get().destroyAccelerationStructureKHR(top_level_as.handle);
	clean_up_scratch_buffers(false);
	if (top_level_as.buffer > -1) storage.destroy_buffer(top_level_as.buffer);

	for (BLAS& blas : bottom_level_as)
	{
		vmc.logical_device.get().destroyAccelerationStructureKHR(blas.handle);
		if (top_level_as.buffer > -1) storage.destroy_buffer(blas.buffer);
	}
	bottom_level_as.clear();
	instances.clear();
}

void AccelerationStructureBuilder::clean_up_scratch_buffers(bool keep_dynamic)
{
	for (BLAS& blas : bottom_level_as)
	{
		if (blas.scratch_buffer > -1 && (!keep_dynamic || !blas.dynamic))
		{
			storage.destroy_buffer(blas.scratch_buffer);
			blas.scratch_buffer = -1;
		}
	}
	if (top_level_as.scratch_buffer > -1 && !keep_dynamic) storage.destroy_buffer(top_level_as.scratch_buffer);
	if (instances_buffer > -1 && !keep_dynamic) storage.destroy_buffer(instances_buffer);
}

uint32_t AccelerationStructureBuilder::add_blas(const std::string& buffer_name, const BLASData& blas_data)
{
	Buffer& vertex_buffer = storage.get_buffer(blas_data.vertex_buffer_id);
	Buffer& index_buffer = storage.get_buffer(blas_data.index_buffer_id);

	vk::DeviceOrHostAddressConstKHR vertex_buffer_device_adress(vertex_buffer.get_device_address());
	vk::DeviceOrHostAddressConstKHR index_buffer_device_adress(index_buffer.get_device_address());

	uint32_t blas_idx = bottom_level_as.size();
	bottom_level_as.emplace_back();
	BLAS& blas = bottom_level_as[blas_idx];
	blas.dynamic = blas_data.dynamic;
	for (uint32_t i = 0; i < blas_data.index_offsets.size(); ++i)
	{
		vk::AccelerationStructureBuildRangeInfoKHR asbri;
		asbri.primitiveCount = blas_data.index_counts.empty() ? index_buffer.get_element_count() / 3 : blas_data.index_counts[i] / 3;
		asbri.primitiveOffset = sizeof(uint32_t) * blas_data.index_offsets[i];
		asbri.firstVertex = 0;
		asbri.transformOffset = 0;
		blas.asbris.push_back(asbri);
		blas.num_triangles.push_back(asbri.primitiveCount);

		vk::AccelerationStructureGeometryKHR asg;
		asg.flags = vk::GeometryFlagBitsKHR::eOpaque;
		asg.geometryType = vk::GeometryTypeKHR::eTriangles;
		asg.geometry.triangles.vertexFormat = vk::Format::eR32G32B32Sfloat;
		asg.geometry.triangles.vertexData = vertex_buffer_device_adress;
		asg.geometry.triangles.maxVertex = vertex_buffer.get_element_count();
		asg.geometry.triangles.vertexStride = blas_data.vertex_stride;
		asg.geometry.triangles.indexType = vk::IndexType::eUint32;
		asg.geometry.triangles.indexData = index_buffer_device_adress;
		asg.geometry.triangles.transformData.deviceAddress = 0;
		asg.geometry.triangles.transformData.hostAddress = nullptr;
		blas.asgs.push_back(asg);
	}

	blas.asbgi.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
	blas.asbgi.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
	blas.asbgi.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
	blas.asbgi.geometryCount = blas.asgs.size();
	blas.asbgi.pGeometries = blas.asgs.data();

	vk::AccelerationStructureBuildSizesInfoKHR asbsi = vmc.logical_device.get().getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, blas.asbgi, blas.num_triangles);
	blas.buffer = storage.add_buffer(buffer_name, asbsi.accelerationStructureSize, vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR, true, QueueFamilyFlags::Compute | QueueFamilyFlags::Graphics | QueueFamilyFlags::Transfer);

	blas.asci.buffer = storage.get_buffer(blas.buffer).get();
	blas.asci.size = asbsi.accelerationStructureSize;
	blas.asci.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
	blas.handle = vmc.logical_device.get().createAccelerationStructureKHR(blas.asci);

	vk::AccelerationStructureDeviceAddressInfoKHR asdai;
	asdai.accelerationStructure = blas.handle;

	blas.device_address = vmc.logical_device.get().getAccelerationStructureAddressKHR(&asdai);

	const ScratchBuffer scratch = create_scratch_buffer(buffer_name, asbsi.buildScratchSize);
	blas.scratch_buffer = scratch.buffer;

	blas.asbgi.dstAccelerationStructure = blas.handle;
	blas.asbgi.scratchData.deviceAddress = scratch.device_address;

	update_blas(blas_idx);

	return blas_idx;
}

void AccelerationStructureBuilder::update_blas(uint32_t blas_idx)
{
	blas_update_indices.emplace(blas_idx);
}

uint32_t AccelerationStructureBuilder::add_instance(uint32_t blas_idx, const vk::TransformMatrixKHR& M, uint32_t custom_index, uint8_t mask)
{
	vk::AccelerationStructureInstanceKHR instance;
	instance.transform = M;
	instance.accelerationStructureReference = bottom_level_as[blas_idx].device_address;
	instance.instanceCustomIndex = custom_index;
	instance.setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
	instance.mask = mask;
	instances.push_back(instance);
	return instances.size() - 1;
}

void AccelerationStructureBuilder::update_instance(uint32_t instance_idx, const vk::TransformMatrixKHR& M)
{
	instances[instance_idx].transform = M;
	storage.get_buffer(instances_buffer).update_data(&instances[instance_idx], 1, instance_idx);
}

void AccelerationStructureBuilder::construct(vk::CommandBuffer& cb, QueueFamilyFlags build_queue, const std::string& buffer_name)
{
	instances_buffer = storage.add_buffer(buffer_name + " instances (vkte internal)", instances, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR, true, QueueFamilyFlags::Compute | QueueFamilyFlags::Graphics | QueueFamilyFlags::Transfer);

	vk::DeviceOrHostAddressConstKHR instance_data_device_address;
	instance_data_device_address.deviceAddress = storage.get_buffer(instances_buffer).get_device_address();

	top_level_as.asg.geometryType = vk::GeometryTypeKHR::eInstances;
	top_level_as.asg.flags = vk::GeometryFlagBitsKHR::eOpaque;
	top_level_as.asg.geometry.instances.sType = vk::StructureType::eAccelerationStructureGeometryInstancesDataKHR;
	top_level_as.asg.geometry.instances.arrayOfPointers = VK_FALSE;
	top_level_as.asg.geometry.instances.data = instance_data_device_address;

	top_level_as.asbgi.type = vk::AccelerationStructureTypeKHR::eTopLevel;
	top_level_as.asbgi.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
	top_level_as.asbgi.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
	top_level_as.asbgi.geometryCount = 1;
	top_level_as.asbgi.pGeometries = &top_level_as.asg;

	top_level_as.primitive_count = instances.size();

	vk::AccelerationStructureBuildSizesInfoKHR asbsi = vmc.logical_device.get().getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, top_level_as.asbgi, top_level_as.primitive_count);

	top_level_as.buffer = storage.add_buffer(buffer_name, asbsi.accelerationStructureSize, vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR, true, QueueFamilyFlags::Compute | QueueFamilyFlags::Graphics | QueueFamilyFlags::Transfer);

	top_level_as.asci.buffer = storage.get_buffer(top_level_as.buffer).get();
	top_level_as.asci.size = asbsi.accelerationStructureSize;
	top_level_as.asci.type = vk::AccelerationStructureTypeKHR::eTopLevel;
	top_level_as.handle = vmc.logical_device.get().createAccelerationStructureKHR(top_level_as.asci);

	vk::AccelerationStructureDeviceAddressInfoKHR asdai;
	asdai.accelerationStructure = top_level_as.handle;
	top_level_as.device_address = vmc.logical_device.get().getAccelerationStructureAddressKHR(&asdai);

	wdsas.accelerationStructureCount = 1;
	wdsas.pAccelerationStructures = &(top_level_as.handle);
	storage.get_buffer(top_level_as.buffer).pNext = &(wdsas);

	const ScratchBuffer scratch = create_scratch_buffer(buffer_name, asbsi.buildScratchSize);
	top_level_as.scratch_buffer = scratch.buffer;

	top_level_as.asbgi.dstAccelerationStructure = top_level_as.handle;
	top_level_as.asbgi.scratchData.deviceAddress = scratch.device_address;

	top_level_as.asbri.primitiveCount = instances.size();
	top_level_as.asbri.primitiveOffset = 0;
	top_level_as.asbri.firstVertex = 0;
	top_level_as.asbri.transformOffset = 0;
	update_tlas(cb, build_queue);
}

void AccelerationStructureBuilder::update_tlas(vk::CommandBuffer& cb, QueueFamilyFlags build_queue)
{
	std::vector<vk::AccelerationStructureBuildGeometryInfoKHR> asbgis;
	std::vector<vk::AccelerationStructureBuildRangeInfoKHR*> pasbris;
	std::vector<vk::BufferMemoryBarrier2> blas_memory_barriers;
	for (uint32_t blas_idx : blas_update_indices)
	{
		BLAS& blas = bottom_level_as[blas_idx];
		asbgis.push_back(blas.asbgi);
		pasbris.push_back(blas.asbris.data());
		blas_memory_barriers.push_back(vk::BufferMemoryBarrier2(vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureWriteKHR, vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureReadKHR, vmc.queue_families.get(build_queue), vmc.queue_families.get(build_queue), storage.get_buffer(blas.buffer).get(), 0, storage.get_buffer(blas.buffer).get_byte_size()));
	}
	blas_update_indices.clear();
	cb.buildAccelerationStructuresKHR(asbgis, pasbris);
	vk::DependencyInfo blas_dependency_info;
	blas_dependency_info.dependencyFlags = vk::DependencyFlagBits::eDeviceGroup;
	blas_dependency_info.bufferMemoryBarrierCount = blas_memory_barriers.size();
	blas_dependency_info.pBufferMemoryBarriers = blas_memory_barriers.data();
	cb.pipelineBarrier2(blas_dependency_info);
	cb.buildAccelerationStructuresKHR({top_level_as.asbgi}, {&top_level_as.asbri});
	const vkte::Buffer& buffer = storage.get_buffer(top_level_as.buffer);
	vk::BufferMemoryBarrier2 barrier = vk::BufferMemoryBarrier2(vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureWriteKHR, vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eAccelerationStructureReadKHR, vmc.queue_families.get(build_queue), vmc.queue_families.get(build_queue), buffer.get(), 0, buffer.get_byte_size());
	vk::DependencyInfo tlas_dependency_info;
	tlas_dependency_info.dependencyFlags = vk::DependencyFlagBits::eDeviceGroup;
	tlas_dependency_info.bufferMemoryBarrierCount = 1;
	tlas_dependency_info.pBufferMemoryBarriers = &barrier;
	cb.pipelineBarrier2(tlas_dependency_info);
}
} // namespace vkte
