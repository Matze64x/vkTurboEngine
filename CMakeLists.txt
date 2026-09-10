cmake_minimum_required(VERSION 3.19)
project(vkTurboEngine)
set(CMAKE_CXX_STANDARD 20)

option(ENABLE_VKTE_LOGGING "Enable vkte logging" OFF)

set(SDL3_DIR "${PROJECT_SOURCE_DIR}/dependencies/SDL3")
set(SPDLOG_DIR "${PROJECT_SOURCE_DIR}/dependencies/spdlog")
set(SLANG_DIR "${PROJECT_SOURCE_DIR}/dependencies/slang")

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

set(SOURCE_FILES
	src/vkte/command_pool.cpp
	src/vkte/device_timer.cpp
	src/vkte/engine.cpp
	src/vkte/resource_declarations.cpp
	src/vkte/extensions_handler.cpp
	src/vkte/image.cpp
	src/vkte/instance.cpp
	src/vkte/logical_device.cpp
	src/vkte/physical_device.cpp
	src/vkte/acceleration_structure_builder.cpp
	src/vkte/pipeline.cpp
	src/vkte/queue_families.cpp
	src/vkte/shader.cpp
	src/vkte/shader_repository.cpp
	src/vkte/storage.cpp
	src/vkte/synchronization.cpp
	src/vkte/thread_manager.cpp
	src/vkte/vulkan_command_context.cpp
	src/vkte/vulkan_main_context.cpp
)

add_subdirectory("${SDL3_DIR}")
add_subdirectory("${SPDLOG_DIR}")

set(SLANG_ENABLE_SLANGD OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANGI OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANGC OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANGRT OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_REPLAYER OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_GFX OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANG_RHI OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_DXIL OFF CACHE BOOL "" FORCE)
set(SLANG_SLANG_LLVM_FLAVOR "DISABLE" CACHE STRING "" FORCE)
add_subdirectory("${SLANG_DIR}")

add_library(vkte_headless ${SOURCE_FILES})
target_include_directories(vkte_headless PUBLIC
	"${PROJECT_SOURCE_DIR}/include/"
	"${PROJECT_SOURCE_DIR}/private/"
	"${PROJECT_SOURCE_DIR}/dependencies/VulkanMemoryAllocator/include/"
	"${PROJECT_SOURCE_DIR}/dependencies/Vulkan_Headers/include/"
)
target_link_libraries(vkte_headless PUBLIC
	spdlog::spdlog
)
target_link_libraries(vkte_headless PRIVATE
	slang
)
target_compile_definitions(vkte_headless PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 VULKAN_HPP_STORAGE_SHARED=1 IMGUI_IMPL_VULKAN_NO_PROTOTYPES=1)
if (ENABLE_VKTE_LOGGING)
	target_compile_definitions(vkte_headless PUBLIC VKTE_LOGGING=1)
endif()
target_compile_definitions(vkte_headless PRIVATE VULKAN_HPP_STORAGE_SHARED_EXPORT=1)
target_precompile_headers(vkte_headless PRIVATE
	<vulkan/vulkan.hpp>
	<vk_mem_alloc.h>
	<spdlog/spdlog.h>
)

add_library(vkte_window ${SOURCE_FILES}
	src/vkte/swapchain.cpp
	src/vkte_window/event_handler.cpp
	src/vkte_window/ui.cpp
	src/vkte_window/window.cpp
	"${PROJECT_SOURCE_DIR}/dependencies/imgui/imgui.cpp"
	"${PROJECT_SOURCE_DIR}/dependencies/imgui/backends/imgui_impl_sdl3.cpp"
	"${PROJECT_SOURCE_DIR}/dependencies/imgui/backends/imgui_impl_vulkan.cpp"
	"${PROJECT_SOURCE_DIR}/dependencies/imgui/imgui_draw.cpp"
	"${PROJECT_SOURCE_DIR}/dependencies/imgui/imgui_widgets.cpp"
	"${PROJECT_SOURCE_DIR}/dependencies/imgui/imgui_tables.cpp"
)
target_include_directories(vkte_window PUBLIC
	"${PROJECT_SOURCE_DIR}/include/"
	"${PROJECT_SOURCE_DIR}/private/"
	"${SDL3_DIR}/include/"
	"${PROJECT_SOURCE_DIR}/dependencies/imgui/"
	"${PROJECT_SOURCE_DIR}/dependencies/VulkanMemoryAllocator/include/"
	"${PROJECT_SOURCE_DIR}/dependencies/Vulkan_Headers/include/"
)
target_link_libraries(vkte_window PUBLIC
	SDL3::SDL3
	spdlog::spdlog
)
target_link_libraries(vkte_window PRIVATE
	slang
)
target_compile_definitions(vkte_window PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 VULKAN_HPP_STORAGE_SHARED=1 IMGUI_IMPL_VULKAN_NO_PROTOTYPES=1 ENABLE_VKTE_WINDOW=1)
if (ENABLE_VKTE_LOGGING)
	target_compile_definitions(vkte_window PUBLIC VKTE_LOGGING=1)
endif()
target_compile_definitions(vkte_window PRIVATE VULKAN_HPP_STORAGE_SHARED_EXPORT=1)
target_precompile_headers(vkte_window PRIVATE
	<vulkan/vulkan.hpp>
	<vk_mem_alloc.h>
	<spdlog/spdlog.h>
)
# imgui_impl_vulkan.cpp needs VK_NO_PROTOTYPES defined
set_source_files_properties(
	"${PROJECT_SOURCE_DIR}/dependencies/imgui/backends/imgui_impl_vulkan.cpp"
	PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)

add_library(vkte::headless ALIAS vkte_headless)
add_library(vkte::window ALIAS vkte_window)
