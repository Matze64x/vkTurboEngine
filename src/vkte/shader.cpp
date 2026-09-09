#include "vkte/shader.hpp"



namespace vkte
{
Shader::Shader(const std::string& name, vk::ShaderStageFlagBits stage_flag, const std::string& entry_point) : name(name), stage_flag(stage_flag), entry_point(entry_point)
{}

const std::vector<uint32_t>& Shader::get_spec_entries_data() const
{
	return spec_entries_data;
}

const std::vector<vk::SpecializationMapEntry>& Shader::get_spec_entries() const
{
	return spec_entries;
}
} // namespace vkte
