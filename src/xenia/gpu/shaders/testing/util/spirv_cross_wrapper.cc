/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/shaders/testing/util/spirv_cross_wrapper.h"

#include <spirv_cross/spirv_cross_c.h>
#include <vkd3d/vkd3d_shader.h>

#include "xenia/base/logging.h"

namespace xe {
namespace gpu {
namespace shaders {
namespace testing {

thread_local std::string SPIRVCrossWrapper::last_error_;

std::vector<uint32_t> SPIRVCrossWrapper::DXILToSPIRV(
    const std::vector<uint8_t>& dxil) {
  last_error_ = "DXIL to SPIR-V conversion not yet implemented";
  XELOGE(
      "DXIL to SPIR-V requires SPIRV-Cross with HLSL support (or alternative "
      "path)");

  // TODO: Implement conversion
  // This may require:
  // 1. Using DXC's built-in SPIR-V backend, OR
  // 2. DXIL -> HLSL decompilation -> GLSL -> SPIR-V, OR
  // 3. Using a specialized DXIL to SPIR-V translator

  // Note: SPIRV-Cross primarily goes SPIR-V -> HLSL, not the reverse
  // We may need to use DXC with -spirv flag instead
  return {};
}

std::vector<uint32_t> SPIRVCrossWrapper::DXBCToSPIRV(
    const std::vector<uint8_t>& dxbc) {
  last_error_.clear();

  if (dxbc.empty()) {
    last_error_ = "Empty DXBC binary";
    return {};
  }

  // Set up vkd3d-shader source structure
  struct vkd3d_shader_code dxbc_code = {};
  dxbc_code.code = dxbc.data();
  dxbc_code.size = dxbc.size();

  // Set up compilation info
  struct vkd3d_shader_compile_info compile_info = {};
  compile_info.type = VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO;
  compile_info.source = dxbc_code;
  compile_info.source_type = VKD3D_SHADER_SOURCE_DXBC_TPF;
  compile_info.target_type = VKD3D_SHADER_TARGET_SPIRV_BINARY;
  compile_info.options = nullptr;
  compile_info.option_count = 0;
  compile_info.log_level = VKD3D_SHADER_LOG_INFO;
  compile_info.source_name = "dxbc_shader";

  // Compile DXBC to SPIR-V
  struct vkd3d_shader_code spirv_code = {};
  char* messages = nullptr;
  int ret = vkd3d_shader_compile(&compile_info, &spirv_code, &messages);

  if (messages) {
    if (ret < 0) {
      last_error_ = "DXBC to SPIR-V compilation failed:\n";
      last_error_ += messages;
      XELOGE("vkd3d-shader error: {}", messages);
    } else if (strlen(messages) > 0) {
      XELOGW("vkd3d-shader warnings: {}", messages);
    }
    vkd3d_shader_free_messages(messages);
  }

  if (ret < 0) {
    return {};
  }

  // Copy SPIR-V code to vector
  std::vector<uint32_t> spirv;
  if (spirv_code.code && spirv_code.size > 0) {
    size_t word_count = spirv_code.size / sizeof(uint32_t);
    const uint32_t* spirv_words =
        reinterpret_cast<const uint32_t*>(spirv_code.code);
    spirv.assign(spirv_words, spirv_words + word_count);
    vkd3d_shader_free_shader_code(&spirv_code);
  }

  return spirv;
}

bool SPIRVCrossWrapper::ValidateSPIRV(const std::vector<uint32_t>& spirv,
                                      std::string* error) {
  last_error_.clear();

  if (spirv.empty()) {
    last_error_ = "Empty SPIR-V binary";
    if (error) *error = last_error_;
    return false;
  }

  // Check magic number
  if (spirv[0] != 0x07230203) {
    last_error_ = "Invalid SPIR-V magic number";
    if (error) *error = last_error_;
    return false;
  }

  // TODO: Use spirv-val for full validation
  // For now, just basic checks
  XELOGW("Full SPIR-V validation not yet implemented, only basic checks");

  // Check size is word-aligned
  if (spirv.size() < 5) {
    last_error_ = "SPIR-V binary too small";
    if (error) *error = last_error_;
    return false;
  }

  return true;
}

std::string SPIRVCrossWrapper::DisassembleSPIRV(
    const std::vector<uint32_t>& spirv) {
  // TODO: Implement using spirv-dis or SPIRV-Cross
  XELOGW("SPIR-V disassembly not yet implemented");
  return "SPIR-V disassembly not implemented";
}

std::vector<SPIRVCrossWrapper::DescriptorBinding>
SPIRVCrossWrapper::ReflectDescriptorBindings(
    const std::vector<uint32_t>& spirv) {
  std::vector<DescriptorBinding> bindings;
  last_error_.clear();

  if (spirv.empty()) {
    last_error_ = "Empty SPIR-V binary";
    return bindings;
  }

  // Use SPIRV-Cross C API
  spvc_context context = nullptr;
  spvc_parsed_ir ir = nullptr;
  spvc_compiler compiler = nullptr;
  spvc_resources resources = nullptr;

  if (spvc_context_create(&context) != SPVC_SUCCESS) {
    last_error_ = "Failed to create SPVC context";
    return bindings;
  }

  // Parse SPIR-V
  if (spvc_context_parse_spirv(context, spirv.data(), spirv.size(), &ir) !=
      SPVC_SUCCESS) {
    last_error_ = std::string("Failed to parse SPIR-V: ") +
                  spvc_context_get_last_error_string(context);
    spvc_context_destroy(context);
    return bindings;
  }

  // Create compiler
  if (spvc_context_create_compiler(context, SPVC_BACKEND_NONE, ir,
                                   SPVC_CAPTURE_MODE_TAKE_OWNERSHIP,
                                   &compiler) != SPVC_SUCCESS) {
    last_error_ = std::string("Failed to create compiler: ") +
                  spvc_context_get_last_error_string(context);
    spvc_context_destroy(context);
    return bindings;
  }

  // Get shader resources
  if (spvc_compiler_create_shader_resources(compiler, &resources) !=
      SPVC_SUCCESS) {
    last_error_ = std::string("Failed to get shader resources: ") +
                  spvc_context_get_last_error_string(context);
    spvc_context_destroy(context);
    return bindings;
  }

  // Helper lambda to process a resource list
  auto process_resources = [&](const spvc_reflected_resource* list,
                               size_t count, DescriptorBinding::Type type) {
    for (size_t i = 0; i < count; ++i) {
      DescriptorBinding binding;
      binding.name = list[i].name;
      binding.type = type;
      binding.set = spvc_compiler_get_decoration(compiler, list[i].id,
                                                 SpvDecorationDescriptorSet);
      binding.binding = spvc_compiler_get_decoration(compiler, list[i].id,
                                                     SpvDecorationBinding);
      bindings.push_back(binding);
    }
  };

  // Get uniform buffers
  const spvc_reflected_resource* uniform_buffers = nullptr;
  size_t uniform_buffer_count = 0;
  spvc_resources_get_resource_list_for_type(
      resources, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, &uniform_buffers,
      &uniform_buffer_count);
  process_resources(uniform_buffers, uniform_buffer_count,
                    DescriptorBinding::UNIFORM_BUFFER);

  // Get storage buffers
  const spvc_reflected_resource* storage_buffers = nullptr;
  size_t storage_buffer_count = 0;
  spvc_resources_get_resource_list_for_type(
      resources, SPVC_RESOURCE_TYPE_STORAGE_BUFFER, &storage_buffers,
      &storage_buffer_count);
  process_resources(storage_buffers, storage_buffer_count,
                    DescriptorBinding::STORAGE_BUFFER);

  // Get sampled images
  const spvc_reflected_resource* sampled_images = nullptr;
  size_t sampled_image_count = 0;
  spvc_resources_get_resource_list_for_type(
      resources, SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, &sampled_images,
      &sampled_image_count);
  process_resources(sampled_images, sampled_image_count,
                    DescriptorBinding::SAMPLED_IMAGE);

  // Get storage images
  const spvc_reflected_resource* storage_images = nullptr;
  size_t storage_image_count = 0;
  spvc_resources_get_resource_list_for_type(
      resources, SPVC_RESOURCE_TYPE_STORAGE_IMAGE, &storage_images,
      &storage_image_count);
  process_resources(storage_images, storage_image_count,
                    DescriptorBinding::STORAGE_IMAGE);

  // Get separate images
  const spvc_reflected_resource* separate_images = nullptr;
  size_t separate_image_count = 0;
  spvc_resources_get_resource_list_for_type(
      resources, SPVC_RESOURCE_TYPE_SEPARATE_IMAGE, &separate_images,
      &separate_image_count);
  process_resources(separate_images, separate_image_count,
                    DescriptorBinding::SAMPLED_IMAGE);

  spvc_context_destroy(context);
  return bindings;
}

const std::string& SPIRVCrossWrapper::GetLastError() { return last_error_; }

}  // namespace testing
}  // namespace shaders
}  // namespace gpu
}  // namespace xe
