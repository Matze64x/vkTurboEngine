#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <vector>

namespace vkte
{
enum class Key : uint32_t
{
	A = 0,
	B = 1,
	C = 2,
	D = 3,
	E = 4,
	F = 5,
	G = 6,
	H = 7,
	I = 8,
	J = 9,
	K = 10,
	L = 11,
	M = 12,
	N = 13,
	O = 14,
	P = 15,
	Q = 16,
	R = 17,
	S = 18,
	T = 19,
	U = 20,
	V = 21,
	W = 22,
	X = 23,
	Y = 24,
	Z = 25,
	Plus = 26,
	Minus = 27,
	Left = 28,
	Right = 29,
	Up = 30,
	Down = 31,
	Shift = 32,
	MouseLeft = 33,
	MouseMiddle = 34,
	MouseRight = 35,
	F1 = 36,
	F2 = 37,
	F3 = 38,
	F4 = 39,
	F5 = 40,
	F6 = 41,
	F7 = 42,
	F8 = 43,
	F9 = 44,
	F10 = 45,
	F11 = 46,
	F12 = 47,
	Zero = 48,
	One = 49,
	Two = 50,
	Three = 51,
	Four = 52,
	Five = 53,
	Six = 54,
	Seven = 55,
	Eight = 56,
	Nine = 57,
	Return = 58,
	Size = 59
};

struct MouseVector
{
	float x = 0.0f;
	float y = 0.0f;
};

// The keys and mouse buttons by what happened to them, so that callers don't handle SDL events themselves. Events that
// ImGui wants (mouse events over its windows, keyboard events while it takes text) don't count, except releasing a key
// whose press counted, so that no key stays down.
class EventHandler
{
public:
	EventHandler();
	// clears what happened during the previous frame; called before the frame's events are dispatched
	void begin_frame();
	void dispatch_event(const SDL_Event& e);
	bool is_key_down(Key key) const;
	// during the frame
	bool was_key_pressed(Key key) const;
	bool was_key_released(Key key) const;
	// in window coordinates, the latest one
	MouseVector get_mouse_position() const;
	// summed over the frame
	MouseVector get_mouse_motion() const;
	MouseVector get_mouse_wheel_motion() const;

private:
	std::vector<bool> down_keys;
	std::vector<bool> pressed_keys;
	std::vector<bool> released_keys;
	std::vector<Key> key_remap;
	MouseVector mouse_position;
	MouseVector mouse_motion;
	MouseVector mouse_wheel_motion;

	void press_key(Key key);
	void release_key(Key key);
	void release_all_keys();
	static uint32_t get_idx(Key key);
	Key remap_key(Key key) const;
	void load_key_remap(const std::string& path);
};
} // namespace vkte
