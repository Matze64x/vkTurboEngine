#pragma once

namespace vkte
{
struct DeviceFeatures
{
	bool dynamic_polygon_mode = false;
	bool dynamic_line_width = false;
	bool ray_tracing = false;
	bool shader_atomic_float = false;
};
} // namespace vkte
