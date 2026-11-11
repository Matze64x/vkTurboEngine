#include "vkte/acceleration_structure_builder.hpp"

#include <span>
#include <utility>
#include "vkte/vkte_log.hpp"

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
	for (CompactionQueries& queries : compaction_queries)
	{
		if (queries.pool) memory_manager.destroy(queries.pool);
		queries = CompactionQueries();
	}
	compacted_blases.clear();
	bottom_level_as.clear();
	blas_generations.clear();
	instance_generations.clear();
	free_blas_indices.clear();
	blas_update_indices.clear();
	instances.clear();
	instance_blases.clear();
	free_instance_indices.clear();
	tlas_outdated = false;
}

BLASHandle AccelerationStructureBuilder::add_blas(const std::string& buffer_name, const BLASData& blas_data)
{
	Buffer& vertex_buffer = memory_manager.get_buffer(blas_data.vertex_buffer_id);
	Buffer& index_buffer = memory_manager.get_buffer(blas_data.index_buffer_id);

	vk::DeviceOrHostAddressConstKHR vertex_buffer_device_adress(vertex_buffer.get_device_address());
	vk::DeviceOrHostAddressConstKHR index_buffer_device_adress(index_buffer.get_device_address());

	BLAS blas;
	blas.dynamic = blas_data.dynamic;
	// a dynamic one is rebuilt in place
	blas.is_compactable = !blas_data.dynamic;
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
		asg.flags = blas_data.geometry_flags.empty() ? vk::GeometryFlagBitsKHR::eOpaque : blas_data.geometry_flags[i];
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

BLASHandle AccelerationStructureBuilder::add_aabb_blas(const std::string& buffer_name, const AABBBLASData& blas_data)
{
	Buffer& aabb_buffer = memory_manager.get_buffer(blas_data.aabb_buffer_id);

	// a box gains nothing from compaction
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

BLASHandle AccelerationStructureBuilder::create_blas(const std::string& buffer_name, BLAS new_blas)
{
	uint32_t blas_idx = bottom_level_as.size();
	if (free_blas_indices.empty())
	{
		bottom_level_as.emplace_back();
		blas_generations.push_back(0);
	}
	else
	{
		blas_idx = free_blas_indices.back();
		free_blas_indices.pop_back();
	}
	BLAS& blas = bottom_level_as[blas_idx];
	blas = std::move(new_blas);
	blas.name = buffer_name;
	blas.asbgi.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
	blas.asbgi.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
	if (blas.is_compactable) blas.asbgi.flags |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction;
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

	const BLASHandle handle{{blas_idx, blas_generations[blas_idx]}};
	update_blas(handle);
	return handle;
}

uint32_t AccelerationStructureBuilder::get_index(BLASHandle blas) const
{
	VKTE_ASSERT(blas.valid() && blas.id < bottom_level_as.size() && blas.generation == blas_generations[blas.id], "vkte: Invalid BLAS handle!");
	return blas.id;
}

bool AccelerationStructureBuilder::is_current(BLASHandle blas) const
{
	return blas.valid() && blas.id < bottom_level_as.size() && blas.generation == blas_generations[blas.id];
}

uint32_t AccelerationStructureBuilder::get_index(InstanceHandle instance) const
{
	VKTE_ASSERT(instance.valid() && instance.id < instances.size() && instance.generation == instance_generations[instance.id], "vkte: Invalid instance handle!");
	return instance.id;
}

vk::DeviceSize AccelerationStructureBuilder::get_blas_byte_size(BLASHandle blas) const
{
	return bottom_level_as[get_index(blas)].asci.size;
}

std::vector<BLASHandle> AccelerationStructureBuilder::take_compacted_blases()
{
	std::vector<BLASHandle> current;
	for (BLASHandle blas : compacted_blases)
	{
		// removed meanwhile
		if (is_current(blas)) current.push_back(blas);
	}
	compacted_blases.clear();
	return current;
}

void AccelerationStructureBuilder::update_blas(BLASHandle handle)
{
	// a static one only has its scratch buffer for its first build
	VKTE_ASSERT(bottom_level_as[get_index(handle)].dynamic || bottom_level_as[get_index(handle)].scratch_buffer.valid(), "vkte: Only dynamic BLASes can be rebuilt!");
	blas_update_indices.emplace(get_index(handle));
	tlas_outdated = true;
}

void AccelerationStructureBuilder::remove_blas(BLASHandle handle)
{
	const uint32_t blas_idx = get_index(handle);
	BLAS& blas = bottom_level_as[blas_idx];
	memory_manager.destroy(blas.handle);
	memory_manager.destroy(blas.buffer);
	if (blas.scratch_buffer.valid()) memory_manager.destroy(blas.scratch_buffer);
	blas = BLAS();
	blas_generations[blas_idx]++;
	blas_update_indices.erase(blas_idx);
	free_blas_indices.push_back(blas_idx);
}

InstanceHandle AccelerationStructureBuilder::add_instance(BLASHandle blas, const vk::TransformMatrixKHR& M, uint32_t custom_index, uint8_t mask, vk::GeometryInstanceFlagsKHR flags)
{
	vk::AccelerationStructureInstanceKHR instance;
	instance.transform = M;
	instance.accelerationStructureReference = bottom_level_as[get_index(blas)].device_address;
	instance.instanceCustomIndex = custom_index;
	instance.setFlags(flags);
	instance.mask = mask;
	tlas_outdated = true;
	if (free_instance_indices.empty())
	{
		instances.push_back(instance);
		instance_blases.push_back(get_index(blas));
		instance_generations.push_back(0);
		return InstanceHandle{{uint32_t(instances.size() - 1), 0}};
	}
	const uint32_t instance_idx = free_instance_indices.back();
	free_instance_indices.pop_back();
	instances[instance_idx] = instance;
	instance_blases[instance_idx] = get_index(blas);
	return InstanceHandle{{instance_idx, instance_generations[instance_idx]}};
}

void AccelerationStructureBuilder::update_instance(InstanceHandle instance, const vk::TransformMatrixKHR& M)
{
	instances[get_index(instance)].transform = M;
	tlas_outdated = true;
}

void AccelerationStructureBuilder::set_instance_blas(InstanceHandle instance, BLASHandle blas)
{
	instances[get_index(instance)].accelerationStructureReference = bottom_level_as[get_index(blas)].device_address;
	instance_blases[get_index(instance)] = get_index(blas);
	tlas_outdated = true;
}

void AccelerationStructureBuilder::remove_instance(InstanceHandle instance)
{
	const uint32_t instance_idx = get_index(instance);
	// an instance without a BLAS is inactive
	instances[instance_idx] = vk::AccelerationStructureInstanceKHR();
	instance_blases[instance_idx] = INVALID_BLAS_INDEX;
	instance_generations[instance_idx]++;
	free_instance_indices.push_back(instance_idx);
	tlas_outdated = true;
}

bool AccelerationStructureBuilder::has_frame_work() const
{
	bool has_queries = false;
	for (const CompactionQueries& queries : compaction_queries) has_queries |= !queries.blases.empty();
	return !blas_update_indices.empty() || tlas_outdated || has_queries;
}

void AccelerationStructureBuilder::begin_frame(vk::CommandBuffer& cb, uint32_t frame_index)
{
	compact_queried_blases(cb, frame_index);
	if (!blas_update_indices.empty())
	{
		std::vector<vk::AccelerationStructureBuildGeometryInfoKHR> asbgis;
		std::vector<vk::AccelerationStructureBuildRangeInfoKHR*> pasbris;
		std::vector<BLASHandle> compactable_blases;
		for (uint32_t blas_idx : blas_update_indices)
		{
			BLAS& blas = bottom_level_as[blas_idx];
			asbgis.push_back(blas.asbgi);
			pasbris.push_back(blas.asbris.data());
			if (blas.is_compactable) compactable_blases.push_back(BLASHandle{{blas_idx, blas_generations[blas_idx]}});
			// only dynamic BLASes are built again
			if (blas.dynamic) continue;
			memory_manager.destroy(blas.scratch_buffer);
			blas.scratch_buffer = ResourceHandle();
		}
		cb.buildAccelerationStructuresKHR(asbgis, pasbris);
		blas_update_indices.clear();
		const vk::MemoryBarrier2 barrier(vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureWriteKHR, vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureReadKHR);
		cb.pipelineBarrier2(vk::DependencyInfo({}, barrier, {}, {}));
		if (!compactable_blases.empty()) query_compacted_sizes(cb, frame_index, compactable_blases);
	}
	if (!tlas_outdated) return;
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

void AccelerationStructureBuilder::query_compacted_sizes(vk::CommandBuffer& cb, uint32_t frame_index, const std::vector<BLASHandle>& blases)
{
	CompactionQueries& queries = compaction_queries[frame_index];
	// this frame's earlier queries are done, since the frame's previous use is over
	if (queries.capacity < blases.size())
	{
		if (queries.pool) memory_manager.destroy(queries.pool);
		queries.capacity = uint32_t(blases.size());
		queries.pool = vmc.logical_device.get().createQueryPool(vk::QueryPoolCreateInfo({}, vk::QueryType::eAccelerationStructureCompactedSizeKHR, queries.capacity));
	}
	std::vector<vk::AccelerationStructureKHR> handles;
	for (BLASHandle blas : blases) handles.push_back(bottom_level_as[blas.id].handle);
	cb.resetQueryPool(queries.pool, 0, uint32_t(blases.size()));
	cb.writeAccelerationStructuresPropertiesKHR(handles, vk::QueryType::eAccelerationStructureCompactedSizeKHR, queries.pool, 0);
	queries.blases = blases;
}

void AccelerationStructureBuilder::compact_queried_blases(vk::CommandBuffer& cb, uint32_t frame_index)
{
	CompactionQueries& queries = compaction_queries[frame_index];
	if (queries.blases.empty()) return;
	// written frames_in_flight frames ago by this frame's previous use, which is over
	std::vector<uint64_t> sizes(queries.blases.size());
	VKTE_CHECK(vmc.logical_device.get().getQueryPoolResults(queries.pool, 0, uint32_t(sizes.size()), sizes.size() * sizeof(uint64_t), sizes.data(), sizeof(uint64_t), vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait), "vkte: Failed to read compacted BLAS sizes!");
	bool has_compacted = false;
	for (uint32_t i = 0; i < queries.blases.size(); i++)
	{
		// removed meanwhile
		if (!is_current(queries.blases[i])) continue;
		BLAS& blas = bottom_level_as[queries.blases[i].id];
		Buffer::Settings buffer_settings;
		buffer_settings.byte_size = sizes[i];
		buffer_settings.usage_flags = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR;
		buffer_settings.location = MemoryLocation::DeviceLocal;
		buffer_settings.queues = QueueFamilyFlags::Compute | QueueFamilyFlags::Graphics | QueueFamilyFlags::Transfer;
		const ResourceHandle buffer = memory_manager.get_storage().add_buffer(blas.name + " compacted", buffer_settings);
		vk::AccelerationStructureCreateInfoKHR asci;
		asci.buffer = memory_manager.get_storage().get_buffer(buffer).get();
		asci.size = sizes[i];
		asci.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
		const vk::AccelerationStructureKHR handle = vmc.logical_device.get().createAccelerationStructureKHR(asci);
		cb.copyAccelerationStructureKHR(vk::CopyAccelerationStructureInfoKHR(blas.handle, handle, vk::CopyAccelerationStructureModeKHR::eCompact));
		// frames in flight may still trace the old one
		memory_manager.destroy(blas.handle);
		memory_manager.destroy(blas.buffer);
		blas.handle = handle;
		blas.buffer = buffer;
		blas.asci = asci;
		vk::AccelerationStructureDeviceAddressInfoKHR asdai;
		asdai.accelerationStructure = handle;
		blas.device_address = vmc.logical_device.get().getAccelerationStructureAddressKHR(&asdai);
		compacted_blases.push_back(queries.blases[i]);
		has_compacted = true;
	}
	queries.blases.clear();
	if (!has_compacted) return;
	for (uint32_t i = 0; i < instances.size(); i++)
	{
		if (instance_blases[i] != INVALID_BLAS_INDEX) instances[i].accelerationStructureReference = bottom_level_as[instance_blases[i]].device_address;
	}
	tlas_outdated = true;
	// the copies are done before the TLAS build reads the BLASes
	const vk::MemoryBarrier2 barrier(vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureWriteKHR, vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureReadKHR);
	cb.pipelineBarrier2(vk::DependencyInfo({}, barrier, {}, {}));
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
