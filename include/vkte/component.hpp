#pragma once

namespace vkte
{
class ResourceDeclarations;
class Storage;
struct FrameSettings;

// Derive from this and register with vkte::Engine to let vkte handle the Vulkan resources.
// The derived class still handles its own storage.
class Component
{
public:
	virtual ~Component() = default;
	virtual const char* name() const = 0;
	virtual void declare_resources(ResourceDeclarations& declarations, Storage& storage, const FrameSettings& frame_settings) {}
	virtual void destruct(Storage& storage) {}
};
} // namespace vkte
