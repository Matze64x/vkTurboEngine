#include "vkte/acceleration_structure_builder.hpp"

#include <span>

namespace vkte
{
AccelerationStructureBuilder::AccelerationStructureBuilder(const VulkanMainContext& vmc, MemoryManager& memory_manager) : vmc(vmc), memory_manager(memory_manager) {}

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

	Buffer::Settings scratch_settings;
	scratch_settings.byte_size = build_scratch_size;
	scratch_settings.usage_flags = vk::BufferUsageFlagBits::eStorageBuffer;
	scratch_settings.location = MemoryLocation::DeviceLocal;
	scratch_settings.queues = QueueFamilyFlags::Compute | QueueFamilyFlags::Graphics | QueueFamilyFlags::Transfer;
	scratch_settings.min_alignment = scratch_offset_alignment;
	const ResourceHandle buffer_handle = memory_manager.get_storage().add_buffer(buffer_name + " scratch (vkte internal)", scratch_settings);
	return ScratchBuffer{buffer_handle, memory_manager.get_storage().get_buffer(buffer_handle).get_device_address()};
}

void AccelerationStructureBuilder::destruct()
{
	if (tlas.handle) destroy_tlas();
	for (ResourceHandle& buffer : instance_buffers)
	{
		if (buffer.valid()) memory_manager.destroy(buffer);
		buffer = ResourceHandle();
	}
	for (BLAS& blas : bottom_level_as)
	{
		if (blas.handle) memory_manager.destroy(blas.handle);
		if (blas.buffer.valid()) memory_manager.destroy(blas.buffer);
		if (blas.scratch_buffer.valid()) memory_manager.destroy(blas.scratch_buffer);
	}
	bottom_level_as.clear();
	free_blas_indices.clear();
	blas_update_indices.clear();
	instances.clear();
	tlas_outdated = false;
}

uint32_t AccelerationStructureBuilder::add_blas(const std::string& buffer_name, const BLASData& blas_data)
{
	Buffer& vertex_buffer = memory_manager.get_buffer(blas_data.vertex_buffer_id);
	Buffer& index_buffer = memory_manager.get_buffer(blas_data.index_buffer_id);

	vk::DeviceOrHostAddressConstKHR vertex_buffer_device_adress(vertex_buffer.get_device_address());
	vk::DeviceOrHostAddressConstKHR index_buffer_device_adress(index_buffer.get_device_address());

	BLAS blas;
	blas.dynamic = blas_data.dynamic;
	for (uint32_t i = 0; i < blas_data.index_offsets.size(); ++i)
	{
		vk::AccelerationStructureBuildRangeInfoKHR asbri;
		asbri.primitiveCount = blas_data.index_counts.empty() ? index_buffer.get_element_count() / 3 : blas_data.index_counts[i] / 3;
		asbri.primitiveOffset = sizeof(uint32_t) * blas_data.index_offsets[i];
		asbri.firstVertex = 0;
		asbri.transformOffset = 0;
		blas.asbris.push_back(asbri);
		blas.primitive_counts.push_back(asbri.primitiveCount);

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
	return create_blas(buffer_name, std::move(blas));
}

uint32_t AccelerationStructureBuilder::add_aabb_blas(const std::string& buffer_name, const AABBBLASData& blas_data)
{
	Buffer& aabb_buffer = memory_manager.get_buffer(blas_data.aabb_buffer_id);

	BLAS blas;
	blas.dynamic = blas_data.dynamic;

	vk::AccelerationStructureBuildRangeInfoKHR asbri;
	asbri.primitiveCount = blas_data.aabb_count;
	asbri.primitiveOffset = sizeof(vk::AabbPositionsKHR) * blas_data.aabb_offset;
	asbri.firstVertex = 0;
	asbri.transformOffset = 0;
	blas.asbris.push_back(asbri);
	blas.primitive_counts.push_back(asbri.primitiveCount);

	vk::AccelerationStructureGeometryKHR asg;
	asg.flags = vk::GeometryFlagBitsKHR::eOpaque;
	asg.geometryType = vk::GeometryTypeKHR::eAabbs;
	asg.geometry.aabbs = vk::AccelerationStructureGeometryAabbsDataKHR(vk::DeviceOrHostAddressConstKHR(aabb_buffer.get_device_address()), sizeof(vk::AabbPositionsKHR));
	blas.asgs.push_back(asg);
	return create_blas(buffer_name, std::move(blas));
}

uint32_t AccelerationStructureBuilder::create_blas(const std::string& buffer_name, BLAS new_blas)
{
	uint32_t blas_idx = bottom_level_as.size();
	if (free_blas_indices.empty()) bottom_level_as.emplace_back();
	else
	{
		blas_idx = free_blas_indices.back();
		free_blas_indices.pop_back();
	}
	BLAS& blas = bottom_level_as[blas_idx];
	blas = std::move(new_blas);
	blas.asbgi.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
	blas.asbgi.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
	blas.asbgi.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
	blas.asbgi.geometryCount = blas.asgs.size();
	blas.asbgi.pGeometries = blas.asgs.data();

	vk::AccelerationStructureBuildSizesInfoKHR asbsi = vmc.logical_device.get().getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, blas.asbgi, blas.primitive_counts);
	Buffer::Settings blas_buffer_settings;
	blas_buffer_settings.byte_size = asbsi.accelerationStructureSize;
	blas_buffer_settings.usage_flags = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR;
	blas_buffer_settings.location = MemoryLocation::DeviceLocal;
	blas_buffer_settings.queues = QueueFamilyFlags::Compute | QueueFamilyFlags::Graphics | QueueFamilyFlags::Transfer;
	blas.buffer = memory_manager.get_storage().add_buffer(buffer_name, blas_buffer_settings);

	blas.asci.buffer = memory_manager.get_storage().get_buffer(blas.buffer).get();
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
	tlas_outdated = true;
}

void AccelerationStructureBuilder::remove_blas(uint32_t blas_idx)
{
	BLAS& blas = bottom_level_as[blas_idx];
	memory_manager.destroy(blas.handle);
	memory_manager.destroy(blas.buffer);
	if (blas.scratch_buffer.valid()) memory_manager.destroy(blas.scratch_buffer);
	blas = BLAS();
	blas_update_indices.erase(blas_idx);
	free_blas_indices.push_back(blas_idx);
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
	tlas_outdated = true;
	return instances.size() - 1;
}

void AccelerationStructureBuilder::update_instance(uint32_t instance_idx, const vk::TransformMatrixKHR& M)
{
	instances[instance_idx].transform = M;
	tlas_outdated = true;
}

void AccelerationStructureBuilder::clear_instances()
{
	instances.clear();
	tlas_outdated = true;
}

bool AccelerationStructureBuilder::has_frame_work() const
{
	return !blas_update_indices.empty() || tlas_outdated;
}

void AccelerationStructureBuilder::begin_frame(vk::CommandBuffer& cb, uint32_t frame_index)
{
	if (!blas_update_indices.empty())
	{
		std::vector<vk::AccelerationStructureBuildGeometryInfoKHR> asbgis;
		std::vector<vk::AccelerationStructureBuildRangeInfoKHR*> pasbris;
		for (uint32_t blas_idx : blas_update_indices)
		{
			BLAS& blas = bottom_level_as[blas_idx];
			asbgis.push_back(blas.asbgi);
			pasbris.push_back(blas.asbris.data());
			// only dynamic BLASes are built again
			if (blas.dynamic) continue;
			memory_manager.destroy(blas.scratch_buffer);
			blas.scratch_buffer = ResourceHandle();
		}
		cb.buildAccelerationStructuresKHR(asbgis, pasbris);
		blas_update_indices.clear();
		const vk::MemoryBarrier2 barrier(vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureWriteKHR, vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureReadKHR);
		cb.pipelineBarrier2(vk::DependencyInfo({}, barrier, {}, {}));
	}
	// its size depends on the instance count
	if (!tlas.handle || tlas.primitive_count != instances.size())
	{
		if (tlas.handle) destroy_tlas();
		create_tlas();
	}
	tlas.asg.geometry.instances.data.deviceAddress = write_instances(frame_index);
	cb.buildAccelerationStructuresKHR({tlas.asbgi}, {&tlas.asbri});
	tlas_outdated = false;
}

vk::DeviceAddress AccelerationStructureBuilder::write_instances(uint32_t frame_index)
{
	const vk::AccelerationStructureInstanceKHR empty_instance;
	// a buffer cannot be empty
	const std::span<const std::byte> data = instances.empty() ? std::as_bytes(std::span(&empty_instance, 1)) : std::as_bytes(std::span(instances));
	ResourceHandle& buffer = instance_buffers[frame_index];
	if (buffer.valid() && memory_manager.get_storage().get_buffer(buffer).get_byte_size() == data.size())
	{
		memory_manager.get_storage().get_buffer(buffer).update_data_bytes(data.data(), data.size());
		return memory_manager.get_storage().get_buffer(buffer).get_device_address();
	}
	if (buffer.valid()) memory_manager.destroy(buffer);
	Buffer::Settings settings;
	settings.initial_data = data;
	settings.usage_flags = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
	settings.location = MemoryLocation::BARFallbackHostVisible;
	settings.queues = QueueFamilyFlags::Compute | QueueFamilyFlags::Graphics | QueueFamilyFlags::Transfer;
	buffer = memory_manager.get_storage().add_buffer("tlas instances (vkte internal)", settings);
	return memory_manager.get_storage().get_buffer(buffer).get_device_address();
}

void AccelerationStructureBuilder::create_tlas()
{
	tlas.asg.geometryType = vk::GeometryTypeKHR::eInstances;
	tlas.asg.flags = vk::GeometryFlagBitsKHR::eOpaque;
	tlas.asg.geometry.instances.sType = vk::StructureType::eAccelerationStructureGeometryInstancesDataKHR;
	tlas.asg.geometry.instances.arrayOfPointers = VK_FALSE;

	tlas.asbgi.type = vk::AccelerationStructureTypeKHR::eTopLevel;
	tlas.asbgi.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
	tlas.asbgi.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
	tlas.asbgi.geometryCount = 1;
	tlas.asbgi.pGeometries = &tlas.asg;

	tlas.primitive_count = instances.size();

	vk::AccelerationStructureBuildSizesInfoKHR asbsi = vmc.logical_device.get().getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, tlas.asbgi, tlas.primitive_count);

	Buffer::Settings top_level_as_buffer_settings;
	top_level_as_buffer_settings.byte_size = asbsi.accelerationStructureSize;
	top_level_as_buffer_settings.usage_flags = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR;
	top_level_as_buffer_settings.location = MemoryLocation::DeviceLocal;
	top_level_as_buffer_settings.queues = QueueFamilyFlags::Compute | QueueFamilyFlags::Graphics | QueueFamilyFlags::Transfer;
	tlas.buffer = memory_manager.add_bindless_buffer("tlas", top_level_as_buffer_settings);

	tlas.asci.buffer = memory_manager.get_buffer(tlas.buffer).get();
	tlas.asci.size = asbsi.accelerationStructureSize;
	tlas.asci.type = vk::AccelerationStructureTypeKHR::eTopLevel;
	tlas.handle = vmc.logical_device.get().createAccelerationStructureKHR(tlas.asci);

	tlas.wdsas.accelerationStructureCount = 1;
	tlas.wdsas.pAccelerationStructures = &tlas.handle;
	memory_manager.get_buffer(tlas.buffer).pNext = &tlas.wdsas;

	const ScratchBuffer scratch = create_scratch_buffer("tlas", asbsi.buildScratchSize);
	tlas.scratch_buffer = scratch.buffer;

	tlas.asbgi.dstAccelerationStructure = tlas.handle;
	tlas.asbgi.scratchData.deviceAddress = scratch.device_address;

	tlas.asbri.primitiveCount = instances.size();
	tlas.asbri.primitiveOffset = 0;
	tlas.asbri.firstVertex = 0;
	tlas.asbri.transformOffset = 0;
}

void AccelerationStructureBuilder::destroy_tlas()
{
	memory_manager.destroy(tlas.handle);
	memory_manager.destroy(tlas.buffer);
	memory_manager.destroy(tlas.scratch_buffer);
	tlas = TLAS();
}
} // namespace vkte
