#pragma once

#include <string>
#include "vulkan/vulkan.hpp"
#include "SDL3/SDL_video.h"

namespace vkte
{
class VulkanMainContext;
class Engine;

class Window
{
public:
	Window() = default;
	void hide();
	void show();
	void set_relative_mouse_mode(bool enabled);
	bool get_relative_mouse_mode() const;
	void warp_mouse(float x, float y);
	vk::Extent2D get_pixel_size() const;

private:
	friend class VulkanMainContext;
	friend class Engine;

	void construct(const std::string& title, const uint32_t width, const uint32_t height);
	void destruct();
	SDL_Window* get() const;
	vk::SurfaceKHR create_surface(const vk::Instance& instance);

	SDL_Window* window;
};
} // namespace vkte
