#pragma once

#include <vector>

namespace vkte
{
bool contains(const std::vector<const char*>& names, const char* name);
bool all_available(const std::vector<const char*>& requested, const std::vector<const char*>& available);
} // namespace vkte
