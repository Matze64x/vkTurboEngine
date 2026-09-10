#include "vkte/shader_repository.hpp"

#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <slang.h>
#include "slang-com-ptr.h"
#include "vkte/thread_manager.hpp"
#include "vkte/vkte_log.hpp"

namespace vkte
{
static SlangStage get_slang_stage(vk::ShaderStageFlagBits stage_flag)
{
	if (stage_flag & vk::ShaderStageFlagBits::eVertex) return SLANG_STAGE_VERTEX;
	if (stage_flag & vk::ShaderStageFlagBits::eTessellationControl) return SLANG_STAGE_HULL;
	if (stage_flag & vk::ShaderStageFlagBits::eTessellationEvaluation) return SLANG_STAGE_DOMAIN;
	if (stage_flag & vk::ShaderStageFlagBits::eGeometry) return SLANG_STAGE_GEOMETRY;
	if (stage_flag & vk::ShaderStageFlagBits::eFragment) return SLANG_STAGE_FRAGMENT;
	if (stage_flag & vk::ShaderStageFlagBits::eCompute) return SLANG_STAGE_COMPUTE;
	// Ray tracing stages
	if (stage_flag & vk::ShaderStageFlagBits::eRaygenKHR) return SLANG_STAGE_RAY_GENERATION;
	if (stage_flag & vk::ShaderStageFlagBits::eIntersectionKHR) return SLANG_STAGE_INTERSECTION;
	if (stage_flag & vk::ShaderStageFlagBits::eAnyHitKHR) return SLANG_STAGE_ANY_HIT;
	if (stage_flag & vk::ShaderStageFlagBits::eClosestHitKHR) return SLANG_STAGE_CLOSEST_HIT;
	if (stage_flag & vk::ShaderStageFlagBits::eMissKHR) return SLANG_STAGE_MISS;
	if (stage_flag & vk::ShaderStageFlagBits::eCallableKHR) return SLANG_STAGE_CALLABLE;
	// Mesh shading stages (EXT/NV aliases)
#ifdef VK_EXT_mesh_shader
	if (stage_flag & vk::ShaderStageFlagBits::eTaskEXT) return SLANG_STAGE_AMPLIFICATION;
	if (stage_flag & vk::ShaderStageFlagBits::eMeshEXT) return SLANG_STAGE_MESH;
#endif
#ifdef VK_NV_mesh_shader
	if (stage_flag & vk::ShaderStageFlagBits::eTaskNV) return SLANG_STAGE_AMPLIFICATION;
	if (stage_flag & vk::ShaderStageFlagBits::eMeshNV) return SLANG_STAGE_MESH;
#endif
	VKTE_ASSERT(false, "vkte: Unsupported slang shader stage flag");
	return SLANG_STAGE_NONE;
}

static void log_slang_diagnostics(slang::IBlob* diagnostics)
{
	if (diagnostics && diagnostics->getBufferSize() > 0) VKTE_ERROR("vkte: {}", static_cast<const char*>(diagnostics->getBufferPointer()));
}

static std::string shader_key(const Shader& shader)
{
	return shader.name + "::" + shader.entry_point;
}

static Slang::ComPtr<slang::ISession> create_session(Slang::ComPtr<slang::IGlobalSession> global_session, const std::string& shader_root_dir)
{
	// enable all capabilities to prevent any warnings about implicit upgrades
	slang::CompilerOptionEntry capability_entry{};
	capability_entry.name = slang::CompilerOptionName::Capability;
	capability_entry.value.intValue0 = global_session->findCapability("all");

	slang::TargetDesc target_desc{};
	target_desc.format = SLANG_SPIRV;
	target_desc.profile = global_session->findProfile("spirv_1_5");
	target_desc.forceGLSLScalarBufferLayout = true;
	target_desc.compilerOptionEntries = &capability_entry;
	target_desc.compilerOptionEntryCount = 1;

	const char* search_path = shader_root_dir.c_str();
	slang::SessionDesc session_desc{};
	session_desc.targets = &target_desc;
	session_desc.targetCount = 1;
	session_desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
	session_desc.searchPaths = &search_path;
	session_desc.searchPathCount = 1;

	Slang::ComPtr<slang::ISession> session;
	VKTE_ASSERT(SLANG_SUCCEEDED(global_session->createSession(session_desc, session.writeRef())), "vkte: Failed to create Slang session");
	return session;
}

static Slang::ComPtr<slang::IBlob> compile_to_spirv(const Shader& shader, slang::ISession& session, const std::string& shader_root_dir)
{
	std::filesystem::path shader_file = std::filesystem::path(shader_root_dir) / shader.name;
	VKTE_ASSERT(std::filesystem::exists(shader_file), "vkte: Failed to find shader file \"" + shader.name + "\"");

	std::ifstream shader_stream(shader_file, std::ios::binary);
	VKTE_ASSERT(shader_stream.is_open(), "vkte: Failed to open shader file \"" + shader.name + "\"");
	const std::uintmax_t shader_source_size = std::filesystem::file_size(shader_file);
	std::vector<char> shader_source(shader_source_size);
	shader_stream.read(shader_source.data(), static_cast<std::streamsize>(shader_source_size));
	Slang::ComPtr<slang::IBlob> source_blob;
	source_blob.attach(slang_createBlob(shader_source.data(), shader_source.size()));

	Slang::ComPtr<slang::IBlob> diagnostics;
	slang::IModule* module = session.loadModuleFromSource(shader.name.c_str(), shader_file.string().c_str(), source_blob, diagnostics.writeRef());
	log_slang_diagnostics(diagnostics);
	if (!module)
	{
		VKTE_ERROR("vkte: Failed to load shader module \"{}\"", shader.name);
		return nullptr;
	}

	Slang::ComPtr<slang::IEntryPoint> entry_point;
	diagnostics.setNull();
	if (SLANG_FAILED(module->findAndCheckEntryPoint(shader.entry_point.c_str(), get_slang_stage(shader.stage_flag), entry_point.writeRef(), diagnostics.writeRef())))
	{
		log_slang_diagnostics(diagnostics);
		VKTE_ERROR("vkte: Failed to find entry point \"{}\" in shader \"{}\"", shader.entry_point, shader.name);
		return nullptr;
	}
	log_slang_diagnostics(diagnostics);

	slang::IComponentType* components[] = { module, entry_point };
	Slang::ComPtr<slang::IComponentType> composite;
	diagnostics.setNull();
	if (SLANG_FAILED(session.createCompositeComponentType(components, 2, composite.writeRef(), diagnostics.writeRef())))
	{
		log_slang_diagnostics(diagnostics);
		VKTE_ERROR("vkte: Failed to compose shader \"{}\"", shader.name);
		return nullptr;
	}
	log_slang_diagnostics(diagnostics);

	Slang::ComPtr<slang::IComponentType> linked_program;
	diagnostics.setNull();
	if (SLANG_FAILED(composite->link(linked_program.writeRef(), diagnostics.writeRef())))
	{
		log_slang_diagnostics(diagnostics);
		VKTE_ERROR("vkte: Failed to link shader \"{}\"", shader.name);
		return nullptr;
	}
	log_slang_diagnostics(diagnostics);

	Slang::ComPtr<slang::IBlob> spirv_blob;
	diagnostics.setNull();
	if (SLANG_FAILED(linked_program->getEntryPointCode(0, 0, spirv_blob.writeRef(), diagnostics.writeRef())))
	{
		log_slang_diagnostics(diagnostics);
		VKTE_ERROR("vkte: Failed to compile shader \"{}\" to SPIR-V", shader.name);
		return nullptr;
	}
	log_slang_diagnostics(diagnostics);

	return spirv_blob;
}

ShaderRepository::ShaderRepository(ThreadManager& thread_manager) : thread_manager(thread_manager)
{}

ShaderRepository::~ShaderRepository() = default;

void ShaderRepository::construct(const vk::Device& device, const std::string& shader_root_dir)
{
	this->device = device;
	this->shader_root_dir = shader_root_dir;
	global_sessions.resize(thread_manager.worker_count());
}

void ShaderRepository::destruct()
{
	for (const std::pair<const std::string, vk::ShaderModule>& entry : modules) device.destroyShaderModule(entry.second);
	modules.clear();
}

bool ShaderRepository::compile_all(const std::vector<const Shader*>& shaders)
{
	this->shaders.clear();
	for (const Shader* shader : shaders)
	{
		this->shaders[shader] = vk::SpecializationInfo(shader->get_spec_entries().size(), shader->get_spec_entries().data(), sizeof(uint32_t) * shader->get_spec_entries_data().size(), shader->get_spec_entries_data().data());
	}
	return recompile_all();
}

bool ShaderRepository::recompile_all()
{
	std::vector<const Shader*> unique_shaders;
	std::unordered_set<std::string> seen_keys;
	for (const std::pair<const Shader* const, vk::SpecializationInfo>& shader : shaders)
	{
		if (seen_keys.insert(shader_key(*shader.first)).second) unique_shaders.push_back(shader.first);
	}

	struct CompileResult
	{
		bool success = false;
		std::string key;
		vk::ShaderModule module;
	};

	std::vector<std::future<CompileResult>> futures;
	futures.reserve(unique_shaders.size());
	for (const Shader* shader : unique_shaders)
	{
		futures.push_back(thread_manager.run([this, shader](size_t worker_index) -> CompileResult
		{
			Slang::ComPtr<slang::IGlobalSession>& global_session = global_sessions[worker_index];
			if (!global_session) VKTE_ASSERT(SLANG_SUCCEEDED(slang::createGlobalSession(global_session.writeRef())), "vkte: Failed to create Slang global session");
			Slang::ComPtr<slang::ISession> session = create_session(global_session, shader_root_dir);
			Slang::ComPtr<slang::IBlob> spirv = compile_to_spirv(*shader, *session, shader_root_dir);
			if (!spirv) return {};

			vk::ShaderModuleCreateInfo smci;
			smci.codeSize = spirv->getBufferSize();
			smci.pCode = static_cast<const uint32_t*>(spirv->getBufferPointer());
			return {true, shader_key(*shader), device.createShaderModule(smci)};
		}));
	}

	bool success = true;
	std::vector<CompileResult> results;
	results.reserve(unique_shaders.size());
	for (std::future<CompileResult>& future : futures)
	{
		CompileResult result = future.get();
		if (!result.success)
		{
			success = false;
			continue;
		}
		results.push_back(std::move(result));
	}

	if (!success)
	{
		for (const CompileResult& result : results) device.destroyShaderModule(result.module);
		return false;
	}

	destruct();
	for (CompileResult& result : results) modules[std::move(result.key)] = result.module;
	return true;
}

vk::PipelineShaderStageCreateInfo ShaderRepository::get_shader_stage(const Shader& shader) const
{
	const std::unordered_map<std::string, vk::ShaderModule>::const_iterator it = modules.find(shader_key(shader));
	VKTE_ASSERT(it != modules.end(), "vkte: Shader \"" + shader.name + "\" was never compiled");

	vk::PipelineShaderStageCreateInfo pssci;
	pssci.module = it->second;
	pssci.stage = shader.stage_flag;
	pssci.pName = shader.entry_point.c_str();
	pssci.pSpecializationInfo = &shaders.at(&shader);
	return pssci;
}
} // namespace vkte
