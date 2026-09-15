module vkte;

namespace vkte
{
	[[vk::binding(0, 0)]] StructuredBuffer<uint64_t> gStorageAddressTable;
	[[vk::binding(1, 0)]] StructuredBuffer<uint32_t> gStorageTextureValid;
	[[vk::binding(2, 0)]] Sampler2D gStorageTextures[];

	static const uint kInvalidHandleId = 0xFFFFFFFFu;

	public struct BufferHandle
	{
		uint id = kInvalidHandleId;
		public bool valid() { return id != kInvalidHandleId && gStorageAddressTable[id] != 0; }
	}
	public struct ImageHandle
	{
		uint id = kInvalidHandleId;
		public bool valid() { return id != kInvalidHandleId && gStorageTextureValid[id] != 0; }
	}

	public struct Buffer<T>
	{
		__init(vk::BufferPointer<T> pointer) { buffer_pointer = pointer; }
		vk::BufferPointer<T> buffer_pointer;
		public __subscript(uint index) -> T
		{
			get { return buffer_pointer.getPtr()[index]; }
			set { buffer_pointer.getPtr()[index] = newValue; }
		}
	}

	public Sampler2D get_texture(ImageHandle handle)
	{
		return gStorageTextures[NonUniformResourceIndex(handle.id)];
	}

	public Buffer<T> get_buffer<T>(BufferHandle handle)
	{
		if (!handle.valid()) return Buffer<T>(vk::BufferPointer<T>(0UL));
		return Buffer<T>(vk::BufferPointer<T>(gStorageAddressTable[handle.id]));
	}

	public Ref<T> get_single_element_buffer<T>(BufferHandle handle)
	{
		return vk::BufferPointer<T>(gStorageAddressTable[handle.id]).Get();
	}

	public RaytracingAccelerationStructure get_acceleration_structure(BufferHandle handle)
	{
		return RaytracingAccelerationStructure(gStorageAddressTable[handle.id]);
	}
}
