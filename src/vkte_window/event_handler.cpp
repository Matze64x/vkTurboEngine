#include "vkte_window/event_handler.hpp"

#include "backends/imgui_impl_sdl3.h"
#include "vkte/vkte_log.hpp"
#include <array>
#include <fstream>
#include <sstream>
#include <string_view>

namespace vkte
{
namespace
{
struct KeyMapping
{
	SDL_Keycode sdl_key = SDLK_UNKNOWN; // SDLK_UNKNOWN for keys not driven by a keyboard scancode (mouse buttons)
	std::string_view name;
};

constexpr std::array<KeyMapping, static_cast<size_t>(Key::Size)> make_key_mappings()
{
	std::array<KeyMapping, static_cast<size_t>(Key::Size)> mappings{};
	mappings[static_cast<size_t>(Key::A)] = {SDLK_A, "A"};
	mappings[static_cast<size_t>(Key::B)] = {SDLK_B, "B"};
	mappings[static_cast<size_t>(Key::C)] = {SDLK_C, "C"};
	mappings[static_cast<size_t>(Key::D)] = {SDLK_D, "D"};
	mappings[static_cast<size_t>(Key::E)] = {SDLK_E, "E"};
	mappings[static_cast<size_t>(Key::F)] = {SDLK_F, "F"};
	mappings[static_cast<size_t>(Key::G)] = {SDLK_G, "G"};
	mappings[static_cast<size_t>(Key::H)] = {SDLK_H, "H"};
	mappings[static_cast<size_t>(Key::I)] = {SDLK_I, "I"};
	mappings[static_cast<size_t>(Key::J)] = {SDLK_J, "J"};
	mappings[static_cast<size_t>(Key::K)] = {SDLK_K, "K"};
	mappings[static_cast<size_t>(Key::L)] = {SDLK_L, "L"};
	mappings[static_cast<size_t>(Key::M)] = {SDLK_M, "M"};
	mappings[static_cast<size_t>(Key::N)] = {SDLK_N, "N"};
	mappings[static_cast<size_t>(Key::O)] = {SDLK_O, "O"};
	mappings[static_cast<size_t>(Key::P)] = {SDLK_P, "P"};
	mappings[static_cast<size_t>(Key::Q)] = {SDLK_Q, "Q"};
	mappings[static_cast<size_t>(Key::R)] = {SDLK_R, "R"};
	mappings[static_cast<size_t>(Key::S)] = {SDLK_S, "S"};
	mappings[static_cast<size_t>(Key::T)] = {SDLK_T, "T"};
	mappings[static_cast<size_t>(Key::U)] = {SDLK_U, "U"};
	mappings[static_cast<size_t>(Key::V)] = {SDLK_V, "V"};
	mappings[static_cast<size_t>(Key::W)] = {SDLK_W, "W"};
	mappings[static_cast<size_t>(Key::X)] = {SDLK_X, "X"};
	mappings[static_cast<size_t>(Key::Y)] = {SDLK_Y, "Y"};
	mappings[static_cast<size_t>(Key::Z)] = {SDLK_Z, "Z"};
	mappings[static_cast<size_t>(Key::Plus)] = {SDLK_KP_PLUS, "Plus"};
	mappings[static_cast<size_t>(Key::Minus)] = {SDLK_KP_MINUS, "Minus"};
	mappings[static_cast<size_t>(Key::Left)] = {SDLK_LEFT, "Left"};
	mappings[static_cast<size_t>(Key::Right)] = {SDLK_RIGHT, "Right"};
	mappings[static_cast<size_t>(Key::Up)] = {SDLK_UP, "Up"};
	mappings[static_cast<size_t>(Key::Down)] = {SDLK_DOWN, "Down"};
	mappings[static_cast<size_t>(Key::Shift)] = {SDLK_LSHIFT, "Shift"}; // SDLK_RSHIFT is normalized to SDLK_LSHIFT before lookup
	mappings[static_cast<size_t>(Key::MouseLeft)] = {SDLK_UNKNOWN, "MouseLeft"};
	mappings[static_cast<size_t>(Key::MouseMiddle)] = {SDLK_UNKNOWN, "MouseMiddle"};
	mappings[static_cast<size_t>(Key::MouseRight)] = {SDLK_UNKNOWN, "MouseRight"};
	mappings[static_cast<size_t>(Key::F1)] = {SDLK_F1, "F1"};
	mappings[static_cast<size_t>(Key::F2)] = {SDLK_F2, "F2"};
	mappings[static_cast<size_t>(Key::F3)] = {SDLK_F3, "F3"};
	mappings[static_cast<size_t>(Key::F4)] = {SDLK_F4, "F4"};
	mappings[static_cast<size_t>(Key::F5)] = {SDLK_F5, "F5"};
	mappings[static_cast<size_t>(Key::F6)] = {SDLK_F6, "F6"};
	mappings[static_cast<size_t>(Key::F7)] = {SDLK_F7, "F7"};
	mappings[static_cast<size_t>(Key::F8)] = {SDLK_F8, "F8"};
	mappings[static_cast<size_t>(Key::F9)] = {SDLK_F9, "F9"};
	mappings[static_cast<size_t>(Key::F10)] = {SDLK_F10, "F10"};
	mappings[static_cast<size_t>(Key::F11)] = {SDLK_F11, "F11"};
	mappings[static_cast<size_t>(Key::F12)] = {SDLK_F12, "F12"};
	mappings[static_cast<size_t>(Key::Zero)] = {SDLK_0, "Zero"};
	mappings[static_cast<size_t>(Key::One)] = {SDLK_1, "One"};
	mappings[static_cast<size_t>(Key::Two)] = {SDLK_2, "Two"};
	mappings[static_cast<size_t>(Key::Three)] = {SDLK_3, "Three"};
	mappings[static_cast<size_t>(Key::Four)] = {SDLK_4, "Four"};
	mappings[static_cast<size_t>(Key::Five)] = {SDLK_5, "Five"};
	mappings[static_cast<size_t>(Key::Six)] = {SDLK_6, "Six"};
	mappings[static_cast<size_t>(Key::Seven)] = {SDLK_7, "Seven"};
	mappings[static_cast<size_t>(Key::Eight)] = {SDLK_8, "Eight"};
	mappings[static_cast<size_t>(Key::Nine)] = {SDLK_9, "Nine"};
	mappings[static_cast<size_t>(Key::Return)] = {SDLK_RETURN, "Return"};
	return mappings;
}

constexpr auto key_mappings = make_key_mappings();

constexpr bool all_keys_covered()
{
	for (const KeyMapping& mapping : key_mappings)
	{
		if (mapping.name.empty()) return false;
	}
	return true;
}
static_assert(all_keys_covered(), "Not every Key enum value has an entry in key_mappings");

bool key_from_sdl_keycode(SDL_Keycode sdl_key, Key& out)
{
	if (sdl_key == SDLK_RSHIFT) sdl_key = SDLK_LSHIFT;
	if (sdl_key == SDLK_UNKNOWN) return false;
	for (size_t i = 0; i < key_mappings.size(); i++)
	{
		if (key_mappings[i].sdl_key == sdl_key)
		{
			out = static_cast<Key>(i);
			return true;
		}
	}
	return false;
}

bool key_from_name(std::string_view name, Key& out)
{
	for (size_t i = 0; i < key_mappings.size(); i++)
	{
		if (key_mappings[i].name == name)
		{
			out = static_cast<Key>(i);
			return true;
		}
	}
	return false;
}
} // namespace

EventHandler::EventHandler() : pressed_keys(get_idx(Key::Size), false), released_keys(get_idx(Key::Size), false), key_remap(get_idx(Key::Size))
{
	for (uint32_t i = 0; i < get_idx(Key::Size); i++) key_remap[i] = static_cast<Key>(i);
	load_key_remap("vkte_key_remap");
}

void EventHandler::dispatch_event(SDL_Event e)
{
	ImGui_ImplSDL3_ProcessEvent(&e);
	if (ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard)
	{
		return;
	}
	switch (e.type)
	{
		case SDL_EVENT_MOUSE_MOTION:
			mouse_motion_x = e.motion.xrel;
			mouse_motion_y = e.motion.yrel;
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			mouse_wheel_motion_x = e.wheel.x;
			mouse_wheel_motion_y = e.wheel.y;
			break;
	}
	switch (e.button.button)
	{
		case SDL_BUTTON_LEFT:
			apply_key_event(Key::MouseLeft, e.type);
			break;
		case SDL_BUTTON_MIDDLE:
			apply_key_event(Key::MouseMiddle, e.type);
			break;
		case SDL_BUTTON_RIGHT:
			apply_key_event(Key::MouseRight, e.type);
			break;
	}
	Key key;
	if (key_from_sdl_keycode(e.key.key, key)) apply_key_event(key, e.type);
}

bool EventHandler::is_key_pressed(Key key) const
{
	return pressed_keys[get_idx(key)];
}

bool EventHandler::is_key_released(Key key) const
{
	return released_keys[get_idx(key)];
}

void EventHandler::set_pressed_key(Key key, bool value)
{
	pressed_keys[get_idx(key)] = value;
}

void EventHandler::set_released_key(Key key, bool value)
{
	released_keys[get_idx(key)] = value;
}

void EventHandler::apply_key_event(Key k, uint32_t et)
{
	k = remap_key(k);
	if (et == SDL_EVENT_KEY_DOWN || et == SDL_EVENT_MOUSE_BUTTON_DOWN)
	{
		pressed_keys[get_idx(k)] = true;
		released_keys[get_idx(k)] = false;
	}
	else if (et == SDL_EVENT_KEY_UP || et == SDL_EVENT_MOUSE_BUTTON_UP)
	{
		pressed_keys[get_idx(k)] = false;
		released_keys[get_idx(k)] = true;
	}
}

uint32_t EventHandler::get_idx(Key key)
{
	return static_cast<uint32_t>(key);
}

Key EventHandler::remap_key(Key key) const
{
	return key_remap[get_idx(key)];
}

void EventHandler::load_key_remap(const std::string& path)
{
	std::ifstream file(path);
	if (!file.is_open()) return;
	std::string line;
	while (std::getline(file, line))
	{
		if (line.empty() || line[0] == '#') continue;
		std::istringstream iss(line);
		std::string physical_name, remapped_name;
		if (!(iss >> physical_name >> remapped_name)) continue;
		Key physical_key, remapped_key;
		const bool physical_ok = key_from_name(physical_name, physical_key);
		const bool remapped_ok = key_from_name(remapped_name, remapped_key);
		if (!physical_ok || !remapped_ok)
		{
			VKTE_WARN("vkte: Unknown key name in keybind remap file \"{}\": \"{}\"", path, physical_ok ? remapped_name : physical_name);
			continue;
		}
		key_remap[get_idx(physical_key)] = remapped_key;
	}
}
} // namespace vkte
