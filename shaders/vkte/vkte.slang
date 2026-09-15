module vkte;

namespace vkte
{
	[[vk::binding(0, 0)]] StructuredBuffer<uint64_t> gStorageAddressTable;
	[[vk::binding(1, 0)]] StructuredBuffer<uint32_t> gStorageTextureValid;
	[[vk::binding(2, 0)]] Sampler2D gStorageTextures[];

	public struct BufferHandle
	{
		uint id;
		public bool valid() { return gStorageAddressTable[id] != 0; }
	}
	public struct ImageHandle
	{
		uint id;
		public bool valid() { return gStorageTextureValid[id] != 0; }
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
		return Buffer<T>(vk::BufferPointer<T>(gStorageAddressTable[handle.id]));
	}

	public Ref<T> get_single_element_buffer<T>(BufferHandle handle)
	{
		return vk::BufferPointer<T>(gStorageAddressTable[handle.id]).Get();
	}
}
