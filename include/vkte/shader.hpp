#pragma once

#include <string>
#include "vulkan/vulkan.hpp"

namespace vkte
{
class Shader
{
public:
	Shader() = default;
	Shader(const std::string& name, vk::ShaderStageFlagBits stage_flag, const std::string& entry_point = "main");
	std::string name;
	vk::ShaderStageFlagBits stage_flag;
	std::string entry_point;

	template<typename T>
	void add_specialization_constant(uint32_t id, T data)
	{
		static_assert(sizeof(T) == 4, "Can only use 4 byte specialization constants.");
		spec_entries_data.push_back(std::bit_cast<uint32_t>(data));
		spec_entries.push_back(vk::SpecializationMapEntry(id, 4 * spec_entries.size(), 4));
	}

	const std::vector<uint32_t>& get_spec_entries_data() const;
	const std::vector<vk::SpecializationMapEntry>& get_spec_entries() const;

private:
	std::vector<uint32_t> spec_entries_data;
	std::vector<vk::SpecializationMapEntry> spec_entries;
};
} // namespace vkte
