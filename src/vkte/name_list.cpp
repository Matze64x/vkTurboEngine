#include "vkte/name_list.hpp"

#include <cstring>

namespace vkte
{
bool contains(const std::vector<const char*>& names, const char* name)
{
	for (const char* candidate : names)
	{
		if (strcmp(candidate, name) == 0) return true;
	}
	return false;
}

bool all_available(const std::vector<const char*>& requested, const std::vector<const char*>& available)
{
	for (const char* name : requested)
	{
		if (!contains(available, name)) return false;
	}
	return true;
}
} // namespace vkte
