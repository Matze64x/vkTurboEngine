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
class ThreadManager;

class ShaderRepository
{
public:
	explicit ShaderRepository(ThreadManager& thread_manager);
	~ShaderRepository();

	void construct(const vk::Device& device, const std::string& shader_root_dir);
	void destruct();
	bool compile_all(const std::vector<const Shader*>& shaders);
	bool recompile_all();
	vk::PipelineShaderStageCreateInfo get_shader_stage(const Shader& shader) const;

private:
	vk::Device device;
	std::string shader_root_dir;
	// one Slang global session per ThreadManager worker, created lazily by whichever worker first needs it
	std::vector<Slang::ComPtr<slang::IGlobalSession>> global_sessions;
	std::unordered_map<std::string, vk::ShaderModule> modules;
	std::unordered_map<const Shader*, vk::SpecializationInfo> shaders;
	ThreadManager& thread_manager;
};
} // namespace vkte
