#include "vkte_window/event_handler.hpp"

#include "backends/imgui_impl_sdl3.h"
#include "vkte/vkte_log.hpp"
#include <array>
#include <fstream>
#include <optional>
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

EventHandler::EventHandler() : down_keys(get_idx(Key::Size), false), pressed_keys(get_idx(Key::Size), false), released_keys(get_idx(Key::Size), false), key_remap(get_idx(Key::Size))
{
	for (uint32_t i = 0; i < get_idx(Key::Size); i++) key_remap[i] = static_cast<Key>(i);
	load_key_remap("vkte_key_remap");
}

void EventHandler::begin_frame()
{
	for (uint32_t i = 0; i < get_idx(Key::Size); i++)
	{
		pressed_keys[i] = false;
		released_keys[i] = false;
	}
	mouse_motion = MouseVector();
	mouse_wheel_motion = MouseVector();
}

static std::optional<Key> get_mouse_button_key(uint8_t button)
{
	if (button == SDL_BUTTON_LEFT) return Key::MouseLeft;
	if (button == SDL_BUTTON_MIDDLE) return Key::MouseMiddle;
	if (button == SDL_BUTTON_RIGHT) return Key::MouseRight;
	return std::nullopt;
}

void EventHandler::dispatch_event(const SDL_Event& e)
{
	ImGui_ImplSDL3_ProcessEvent(&e);
	const bool is_mouse_captured = ImGui::GetIO().WantCaptureMouse;
	const bool is_keyboard_captured = ImGui::GetIO().WantCaptureKeyboard;
	Key key;
	switch (e.type)
	{
		case SDL_EVENT_MOUSE_MOTION:
			mouse_position = MouseVector{e.motion.x, e.motion.y};
			if (is_mouse_captured) break;
			mouse_motion.x += e.motion.xrel;
			mouse_motion.y += e.motion.yrel;
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			if (is_mouse_captured) break;
			mouse_wheel_motion.x += e.wheel.x;
			mouse_wheel_motion.y += e.wheel.y;
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
		{
			mouse_position = MouseVector{e.button.x, e.button.y};
			const std::optional<Key> button_key = get_mouse_button_key(e.button.button);
			if (!button_key) break;
			if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) release_key(*button_key);
			else if (!is_mouse_captured) press_key(*button_key);
			break;
		}
		case SDL_EVENT_KEY_DOWN:
			if (!e.key.repeat && !is_keyboard_captured && key_from_sdl_keycode(e.key.key, key)) press_key(key);
			break;
		case SDL_EVENT_KEY_UP:
			if (key_from_sdl_keycode(e.key.key, key)) release_key(key);
			break;
		// the window gets no releases while another one has the focus
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			release_all_keys();
			break;
	}
}

bool EventHandler::is_key_down(Key key) const
{
	return down_keys[get_idx(key)];
}

bool EventHandler::was_key_pressed(Key key) const
{
	return pressed_keys[get_idx(key)];
}

bool EventHandler::was_key_released(Key key) const
{
	return released_keys[get_idx(key)];
}

MouseVector EventHandler::get_mouse_position() const
{
	return mouse_position;
}

MouseVector EventHandler::get_mouse_motion() const
{
	return mouse_motion;
}

MouseVector EventHandler::get_mouse_wheel_motion() const
{
	return mouse_wheel_motion;
}

void EventHandler::press_key(Key key)
{
	key = remap_key(key);
	down_keys[get_idx(key)] = true;
	pressed_keys[get_idx(key)] = true;
}

// a key whose press didn't count isn't released either
void EventHandler::release_key(Key key)
{
	key = remap_key(key);
	if (!down_keys[get_idx(key)]) return;
	down_keys[get_idx(key)] = false;
	released_keys[get_idx(key)] = true;
}

void EventHandler::release_all_keys()
{
	for (uint32_t i = 0; i < get_idx(Key::Size); i++)
	{
		if (down_keys[i]) released_keys[i] = true;
		down_keys[i] = false;
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
