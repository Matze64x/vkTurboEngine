#include "vkte_window/window.hpp"

#include "vkte/vkte_log.hpp"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

namespace vkte
{
void Window::construct(const std::string& title, const uint32_t width, const uint32_t height)
{
#ifdef __linux__
	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland,x11");
#endif
	SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_CURSOR_VISIBLE, "1");
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
	{
		VKTE_ERROR("Failed to initialize SDL: {}", SDL_GetError());
	}
	window = SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
}

void Window::destruct()
{
	SDL_DestroyWindow(window);
	SDL_Quit();
}

void Window::hide()
{
	SDL_HideWindow(window);
}

void Window::show()
{
	SDL_ShowWindow(window);
}

SDL_Window* Window::get() const
{
	return window;
}

vk::SurfaceKHR Window::create_surface(const vk::Instance& instance)
{
	vk::SurfaceKHR surface;
	VKTE_ASSERT(SDL_Vulkan_CreateSurface(window, instance, nullptr, reinterpret_cast<VkSurfaceKHR*>(&surface)), "Failed to create surface!");
	return surface;
}

void Window::set_relative_mouse_mode(bool enabled)
{
	SDL_SetWindowRelativeMouseMode(window, enabled);
}

bool Window::get_relative_mouse_mode() const
{
	return SDL_GetWindowRelativeMouseMode(window);
}

void Window::warp_mouse(float x, float y)
{
	SDL_WarpMouseInWindow(window, x, y);
}

vk::Extent2D Window::get_pixel_size() const
{
	int32_t width = 0;
	int32_t height = 0;
	SDL_GetWindowSizeInPixels(window, &width, &height);
	return vk::Extent2D(width > 0 ? static_cast<uint32_t>(width) : 0u, height > 0 ? static_cast<uint32_t>(height) : 0u);
}
} // namespace vkte
