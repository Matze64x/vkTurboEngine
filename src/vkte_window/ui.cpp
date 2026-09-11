#include "vkte_window/ui.hpp"
#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include "backends/imgui_impl_sdl3.h"
#include "vkte/swapchain.hpp"
#include "vkte/vulkan_command_context.hpp"
#include "vkte/vulkan_main_context.hpp"

namespace vkte
{
void UI::construct(const vkte::VulkanMainContext& vmc, const vkte::Swapchain& swapchain)
{
	std::vector<vk::DescriptorPoolSize> pool_sizes =
	{
		{ vk::DescriptorType::eSampler, 1000 },
		{ vk::DescriptorType::eCombinedImageSampler, 1000 },
		{ vk::DescriptorType::eSampledImage, 1000 },
		{ vk::DescriptorType::eStorageImage, 1000 },
		{ vk::DescriptorType::eUniformTexelBuffer, 1000 },
		{ vk::DescriptorType::eStorageTexelBuffer, 1000 },
		{ vk::DescriptorType::eUniformBuffer, 1000 },
		{ vk::DescriptorType::eStorageBuffer, 1000 },
		{ vk::DescriptorType::eUniformBufferDynamic, 1000 },
		{ vk::DescriptorType::eStorageBufferDynamic, 1000 },
		{ vk::DescriptorType::eInputAttachment, 1000 }
	};

	vk::DescriptorPoolCreateInfo dpci;
	dpci.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
	dpci.maxSets = 1000;
	dpci.poolSizeCount = pool_sizes.size();
	dpci.pPoolSizes = pool_sizes.data();

	imgui_pool = vmc.logical_device.get().createDescriptorPool(dpci);

	VkFormat color_fmt = static_cast<VkFormat>(swapchain.get_color_format());
	VkFormat depth_fmt = static_cast<VkFormat>(swapchain.get_depth_format());

	VkPipelineRenderingCreateInfoKHR pipeline_rendering_info{};
	pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
	pipeline_rendering_info.colorAttachmentCount = 1;
	pipeline_rendering_info.pColorAttachmentFormats = &color_fmt;
	pipeline_rendering_info.depthAttachmentFormat = depth_fmt;
	pipeline_rendering_info.stencilAttachmentFormat = has_stencil(swapchain.get_depth_format()) ? depth_fmt : VK_FORMAT_UNDEFINED;

	ImGui::CreateContext();
	ImGui_ImplSDL3_InitForVulkan(vmc.window.get());
	ImGui_ImplVulkan_InitInfo init_info{};
	init_info.ApiVersion = VK_API_VERSION_1_4;
	init_info.Instance = vmc.instance.get();
	init_info.PhysicalDevice = vmc.physical_device.get();
	init_info.Device = vmc.logical_device.get();
	init_info.Queue = vmc.get_graphics_queue();
	init_info.DescriptorPool = imgui_pool;
	init_info.RenderPass = VK_NULL_HANDLE;
	init_info.UseDynamicRendering = true;
	init_info.PipelineRenderingCreateInfo = pipeline_rendering_info;
	init_info.MinImageCount = swapchain.get_image_count();
	init_info.ImageCount = swapchain.get_image_count();
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_4, [](const char* function_name, void* userData) {
		return VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(VkInstance(userData), function_name);
	}, (void*)(vmc.instance.get()));

	ImGui_ImplVulkan_Init(&init_info);
	ImGui::StyleColorsDark();
}

void UI::destruct(const vkte::VulkanMainContext& vmc)
{
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
	vmc.logical_device.get().destroyDescriptorPool(imgui_pool);
}

void UI::new_frame(const std::string& title)
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();
	ImGui::Begin(title.c_str());
}

void UI::end_frame(vk::CommandBuffer& cb)
{
	ImGui::End();
	ImGui::EndFrame();
	ImGui::Render();
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);
}
} // namespace vkte
