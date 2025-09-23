/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_shader.h"

#include <cstdint>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace gpu {
namespace vulkan {

VulkanShader::VulkanTranslation::~VulkanTranslation() {
  VkShaderModule module = shader_module_.load(std::memory_order_acquire);
  if (module != VK_NULL_HANDLE) {
    const ui::vulkan::VulkanDevice* const vulkan_device =
        static_cast<const VulkanShader&>(shader()).vulkan_device_;
    vulkan_device->functions().vkDestroyShaderModule(vulkan_device->device(),
                                                     module, nullptr);
  }
}

VkShaderModule VulkanShader::VulkanTranslation::GetOrCreateShaderModule() {
  if (!is_valid()) {
    return VK_NULL_HANDLE;
  }

  VkShaderModule existing_module =
      shader_module_.load(std::memory_order_acquire);
  if (existing_module != VK_NULL_HANDLE) {
    return existing_module;
  }

  // Lock for creation
  std::lock_guard<std::mutex> lock(optimization_mutex_);

  // Check again after acquiring lock
  existing_module = shader_module_.load(std::memory_order_acquire);
  if (existing_module != VK_NULL_HANDLE) {
    return existing_module;
  }

  const ui::vulkan::VulkanDevice* const vulkan_device =
      static_cast<const VulkanShader&>(shader()).vulkan_device_;

  // Use optimized binary if available, otherwise use the original
  const std::vector<uint8_t>& binary_to_use =
      !optimized_binary_.empty() ? optimized_binary_ : translated_binary();

  VkShaderModuleCreateInfo shader_module_create_info;
  shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_module_create_info.pNext = nullptr;
  shader_module_create_info.flags = 0;
  shader_module_create_info.codeSize = binary_to_use.size();
  shader_module_create_info.pCode =
      reinterpret_cast<const uint32_t*>(binary_to_use.data());

  VkShaderModule new_module = VK_NULL_HANDLE;
  if (vulkan_device->functions().vkCreateShaderModule(
          vulkan_device->device(), &shader_module_create_info, nullptr,
          &new_module) != VK_SUCCESS) {
    XELOGE(
        "VulkanShader::VulkanTranslation: Failed to create a Vulkan shader "
        "module for shader {:016X} modification {:016X}",
        shader().ucode_data_hash(), modification());
    MakeInvalid();
    return VK_NULL_HANDLE;
  }

  shader_module_.store(new_module, std::memory_order_release);
  return new_module;
}

void VulkanShader::VulkanTranslation::SetOptimizedBinary(
    const std::vector<uint8_t>& optimized_binary) {
  std::lock_guard<std::mutex> lock(optimization_mutex_);

  // Store the optimized binary
  optimized_binary_ = optimized_binary;

  // If we already have a shader module, we need to recreate it with optimized
  // code
  VkShaderModule old_module = shader_module_.load(std::memory_order_acquire);
  if (old_module != VK_NULL_HANDLE) {
    const ui::vulkan::VulkanDevice* const vulkan_device =
        static_cast<const VulkanShader&>(shader()).vulkan_device_;

    VkShaderModuleCreateInfo shader_module_create_info;
    shader_module_create_info.sType =
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_module_create_info.pNext = nullptr;
    shader_module_create_info.flags = 0;
    shader_module_create_info.codeSize = optimized_binary_.size();
    shader_module_create_info.pCode =
        reinterpret_cast<const uint32_t*>(optimized_binary_.data());

    VkShaderModule new_module = VK_NULL_HANDLE;
    if (vulkan_device->functions().vkCreateShaderModule(
            vulkan_device->device(), &shader_module_create_info, nullptr,
            &new_module) == VK_SUCCESS) {
      // Atomically swap the modules
      shader_module_.store(new_module, std::memory_order_release);

      // Queue the old module for deferred destruction
      // The pipeline cache will destroy these when it's safe (after GPU idle or
      // fence wait)
      pending_destroy_modules_.push_back(old_module);
    }
  }

  // Mark as optimized
  is_optimized_.store(true, std::memory_order_release);
}

VulkanShader::VulkanShader(const ui::vulkan::VulkanDevice* const vulkan_device,
                           const xenos::ShaderType shader_type,
                           const uint64_t ucode_data_hash,
                           const uint32_t* const ucode_dwords,
                           const size_t ucode_dword_count,
                           const std::endian ucode_source_endian)
    : SpirvShader(shader_type, ucode_data_hash, ucode_dwords, ucode_dword_count,
                  ucode_source_endian),
      vulkan_device_(vulkan_device) {
  assert_not_null(vulkan_device);
}

Shader::Translation* VulkanShader::CreateTranslationInstance(
    uint64_t modification) {
  return new VulkanTranslation(*this, modification);
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
