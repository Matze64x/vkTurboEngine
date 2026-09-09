#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "vulkan/vulkan.hpp"
#include "slang-com-ptr.h"
#include "vkte/shader.hpp"

struct ISlangBlob;

namespace slang
{
struct IGlobalSession;
struct ISession;
typedef ISlangBlob IBlob;
} // namespace slang

namespace vkte
{
class ShaderRepository
{
public:
	ShaderRepository();
	~ShaderRepository();

	void construct(const vk::Device& device, const std::string& shader_root_dir);
	void destruct();
	bool compile_all(const std::vector<const Shader*>& shaders);
	bool recompile_all();
	vk::PipelineShaderStageCreateInfo get_shader_stage(const Shader& shader) const;

private:
	vk::Device device;
	std::string shader_root_dir;
	Slang::ComPtr<slang::IGlobalSession> global_session;
	std::unordered_map<std::string, vk::ShaderModule> modules;
	std::unordered_map<const Shader*, vk::SpecializationInfo> shaders;
};
} // namespace vkte
