/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SHADERS_TESTING_UTIL_VULKAN_TEST_DEVICE_H_
#define XENIA_GPU_SHADERS_TESTING_UTIL_VULKAN_TEST_DEVICE_H_

#include <memory>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace xe {
namespace gpu {
namespace shaders {
namespace testing {

// Vulkan test device using RAII wrappers for automatic resource management
class VulkanTestDevice {
 public:
  VulkanTestDevice();
  ~VulkanTestDevice() = default;

  bool Initialize();

  // Get RAII handles
  vk::raii::Context& GetContext() { return context_; }
  vk::raii::Instance& GetInstance() { return *instance_; }
  vk::raii::PhysicalDevice& GetPhysicalDevice() { return *physical_device_; }
  vk::raii::Device& GetDevice() { return *device_; }
  vk::raii::Queue& GetQueue() { return *queue_; }
  uint32_t GetQueueFamilyIndex() const { return queue_family_index_; }

  // Buffer helpers
  vk::raii::Buffer CreateBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage,
                                vk::MemoryPropertyFlags mem_props);

  vk::raii::DeviceMemory AllocateMemory(const vk::raii::Buffer& buffer,
                                        vk::MemoryPropertyFlags mem_props);

  void* Map(const vk::raii::DeviceMemory& memory, vk::DeviceSize size);
  void Unmap(const vk::raii::DeviceMemory& memory);

  // Image helpers
  vk::raii::Image CreateImage(uint32_t width, uint32_t height,
                              vk::Format format, vk::ImageTiling tiling,
                              vk::ImageUsageFlags usage,
                              vk::MemoryPropertyFlags mem_props);

  vk::raii::DeviceMemory AllocateMemory(const vk::raii::Image& image,
                                        vk::MemoryPropertyFlags mem_props);

  vk::raii::ImageView CreateImageView(const vk::raii::Image& image,
                                      vk::Format format,
                                      vk::ImageAspectFlags aspect_flags);

  vk::raii::Sampler CreateSampler();

  // Command buffer helpers
  vk::raii::CommandBuffer BeginSingleTimeCommands();
  void EndSingleTimeCommands(vk::raii::CommandBuffer& cmd);

  // Data transfer helpers
  void UploadToBuffer(const vk::raii::Buffer& buffer,
                      const vk::raii::DeviceMemory& memory, const void* data,
                      size_t size);

  void DownloadFromBuffer(const vk::raii::Buffer& buffer,
                          const vk::raii::DeviceMemory& memory, void* data,
                          size_t size);

  void UploadToImage(const vk::raii::Image& image, uint32_t width,
                     uint32_t height, vk::Format format, const void* data,
                     size_t size);

  void DownloadFromImage(const vk::raii::Image& image, uint32_t width,
                         uint32_t height, vk::Format format, void* data,
                         size_t size);

 private:
  bool CreateInstance();
  bool SelectPhysicalDevice();
  bool CreateDevice();
  bool CreateCommandPool();

  uint32_t FindMemoryType(uint32_t type_filter,
                          vk::MemoryPropertyFlags properties);

  vk::raii::Context context_;
  std::unique_ptr<vk::raii::Instance> instance_;
  std::unique_ptr<vk::raii::PhysicalDevice> physical_device_;
  std::unique_ptr<vk::raii::Device> device_;
  std::unique_ptr<vk::raii::Queue> queue_;
  uint32_t queue_family_index_ = 0;
  std::unique_ptr<vk::raii::CommandPool> command_pool_;
};

}  // namespace testing
}  // namespace shaders
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SHADERS_TESTING_UTIL_VULKAN_TEST_DEVICE_H_
