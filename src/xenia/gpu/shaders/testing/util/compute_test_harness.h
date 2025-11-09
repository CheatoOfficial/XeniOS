/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SHADERS_TESTING_UTIL_COMPUTE_TEST_HARNESS_H_
#define XENIA_GPU_SHADERS_TESTING_UTIL_COMPUTE_TEST_HARNESS_H_

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "xenia/gpu/shaders/testing/util/vulkan_test_device.h"

namespace xe {
namespace gpu {
namespace shaders {
namespace testing {

// High-level harness for testing compute shaders.
// Handles descriptor sets, pipeline creation, buffer management, and dispatch.
//
// Example usage:
//   VulkanTestDevice device;
//   device.Initialize();
//
//   auto spirv = ShaderCompiler::CompileGLSLToSPIRV(shader_src, ...);
//   ComputeTestHarness harness(&device, spirv);
//
//   std::vector<uint32_t> input = {1, 2, 3, 4, 5};
//   harness.SetInputBuffer(0, input);
//   harness.AllocateOutputBuffer(1, input.size() * sizeof(uint32_t));
//
//   harness.Dispatch(1, 1, 1);
//
//   auto output = harness.ReadOutputBuffer<uint32_t>(1);
//   // Validate output...
class ComputeTestHarness {
 public:
  // Create harness with compiled SPIR-V
  ComputeTestHarness(VulkanTestDevice* device,
                     const std::vector<uint32_t>& spirv);
  ~ComputeTestHarness();

  // Set input buffer data (creates buffer and uploads data)
  template <typename T>
  void SetInputBuffer(uint32_t binding, const std::vector<T>& data,
                      uint32_t set = 0) {
    SetInputBufferRaw(binding, data.data(), data.size() * sizeof(T), set);
  }

  // Set uniform buffer data (creates uniform buffer and uploads data)
  template <typename T>
  void SetUniformBuffer(uint32_t binding, const T& data, uint32_t set = 0) {
    SetUniformBufferRaw(binding, &data, sizeof(T), set);
  }

  // Set texel buffer (uniform texel buffer / texture buffer for texelFetch)
  template <typename T>
  void SetTexelBuffer(uint32_t binding, const std::vector<T>& data,
                      vk::Format format, uint32_t set = 0) {
    SetTexelBufferRaw(binding, data.data(), data.size() * sizeof(T), format,
                      set);
  }

  // Allocate output buffer (creates empty buffer for shader to write to)
  void AllocateOutputBuffer(uint32_t binding, size_t byte_size,
                            uint32_t set = 0);

  // Set texture data (creates texture and uploads data)
  template <typename T>
  void SetTexture2D(uint32_t binding, uint32_t width, uint32_t height,
                    vk::Format format, const std::vector<T>& data,
                    uint32_t set = 0) {
    SetTexture2DRaw(binding, width, height, format, data.data(),
                    data.size() * sizeof(T), set);
  }

  // Allocate output image (creates writable 2D image)
  void AllocateOutputImage2D(uint32_t binding, uint32_t width, uint32_t height,
                             vk::Format format, uint32_t set = 0);

  // Read output image back to CPU
  template <typename T>
  std::vector<T> ReadOutputImage2D(uint32_t binding, uint32_t set = 0) {
    auto it = images_.find({set, binding});
    if (it == images_.end()) {
      return {};
    }

    size_t pixel_count = it->second.width * it->second.height;
    size_t bytes_per_pixel = GetFormatSize(it->second.format);
    size_t total_bytes = pixel_count * bytes_per_pixel;

    std::vector<T> result(total_bytes / sizeof(T));
    device_->DownloadFromImage(it->second.image, it->second.width,
                               it->second.height, it->second.format,
                               result.data(), total_bytes);
    return result;
  }

  // Set push constants
  template <typename T>
  void SetPushConstants(const T& data) {
    SetPushConstantsRaw(&data, sizeof(T));
  }

  // Dispatch compute shader
  // group_x/y/z: number of workgroups to dispatch
  void Dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z);

  // Read output buffer back to CPU
  template <typename T>
  std::vector<T> ReadOutputBuffer(uint32_t binding, uint32_t set = 0) {
    auto it = buffers_.find({set, binding});
    if (it == buffers_.end()) {
      return {};
    }

    size_t element_count = it->second.size / sizeof(T);
    std::vector<T> result(element_count);
    device_->DownloadFromBuffer(it->second.buffer, it->second.memory,
                                result.data(), it->second.size);
    return result;
  }

  // Check if harness is ready for dispatch
  bool IsValid() const {
    return *shader_module_ != VK_NULL_HANDLE &&
           (!descriptor_sets_.empty() ? *pipeline_ != VK_NULL_HANDLE : true);
  }

 private:
  void SetInputBufferRaw(uint32_t binding, const void* data, size_t byte_size,
                         uint32_t set);
  void SetUniformBufferRaw(uint32_t binding, const void* data, size_t byte_size,
                           uint32_t set);
  void SetTexelBufferRaw(uint32_t binding, const void* data, size_t byte_size,
                         vk::Format format, uint32_t set);
  void SetTexture2DRaw(uint32_t binding, uint32_t width, uint32_t height,
                       vk::Format format, const void* data, size_t byte_size,
                       uint32_t set);
  void SetPushConstantsRaw(const void* data, size_t byte_size);

  bool CreateShaderModule(const std::vector<uint32_t>& spirv);
  bool CreateDescriptorSetLayout();  // Stub - actual creation in
                                     // EnsureDescriptorSetLayouts
  void EnsureDescriptorSetLayouts();
  void UpdateDescriptorSets();

  size_t GetFormatSize(vk::Format format) const;

  struct BufferInfo {
    vk::raii::Buffer buffer = nullptr;
    vk::raii::DeviceMemory memory = nullptr;
    vk::raii::BufferView buffer_view = nullptr;  // For texel buffers
    size_t size = 0;
    bool is_texel_buffer = false;
    bool is_uniform_buffer = false;
  };

  struct ImageInfo {
    vk::raii::Image image = nullptr;
    vk::raii::DeviceMemory memory = nullptr;
    vk::raii::ImageView view = nullptr;
    vk::raii::Sampler sampler = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    vk::Format format = vk::Format::eUndefined;
    bool is_storage = false;
  };

  struct SetBinding {
    uint32_t set;
    uint32_t binding;
    bool operator<(const SetBinding& other) const {
      if (set != other.set) return set < other.set;
      return binding < other.binding;
    }
  };

  VulkanTestDevice* device_;

  vk::raii::ShaderModule shader_module_ = nullptr;
  std::vector<vk::raii::DescriptorSetLayout> descriptor_set_layouts_;
  vk::raii::PipelineLayout pipeline_layout_ = nullptr;
  vk::raii::Pipeline pipeline_ = nullptr;
  vk::raii::DescriptorPool descriptor_pool_ = nullptr;
  std::vector<vk::raii::DescriptorSet> descriptor_sets_;

  std::map<SetBinding, BufferInfo> buffers_;
  std::map<SetBinding, ImageInfo> images_;

  std::vector<uint8_t> push_constant_data_;
};

}  // namespace testing
}  // namespace shaders
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SHADERS_TESTING_UTIL_COMPUTE_TEST_HARNESS_H_
