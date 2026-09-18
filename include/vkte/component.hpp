#pragma once

namespace vkte
{
class ResourceDeclarations;
class MemoryManager;
struct FrameSettings;

// Derive from this and register with vkte::Engine to let vkte handle the Vulkan resources.
// The derived class still handles its own storage.
class Component
{
public:
	virtual ~Component() = default;
	virtual const char* name() const = 0;
	virtual void declare_resources(ResourceDeclarations& declarations, MemoryManager& memory_manager, const FrameSettings& frame_settings) = 0;
	virtual void destruct(MemoryManager& memory_manager) = 0;
};
} // namespace vkte
