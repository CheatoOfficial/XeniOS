/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/shaders/testing/util/compute_test_harness.h"

#include <cstring>

#include "xenia/base/logging.h"

namespace xe {
namespace gpu {
namespace shaders {
namespace testing {

ComputeTestHarness::ComputeTestHarness(VulkanTestDevice* device,
                                       const std::vector<uint32_t>& spirv)
    : device_(device),
      shader_module_(nullptr),
      pipeline_layout_(nullptr),
      pipeline_(nullptr),
      descriptor_pool_(nullptr) {
  if (!device) {
    XELOGE("ComputeTestHarness: Invalid device");
    return;
  }

  if (!CreateShaderModule(spirv)) {
    XELOGE("ComputeTestHarness: Failed to create shader module");
    return;
  }

  // Descriptor sets, pipeline layout, and pipeline will be created
  // dynamically when resources are bound via UpdateDescriptorSets()
}

ComputeTestHarness::~ComputeTestHarness() {
  // RAII handles cleanup automatically
  buffers_.clear();
  images_.clear();
}

void ComputeTestHarness::SetInputBufferRaw(uint32_t binding, const void* data,
                                           size_t byte_size, uint32_t set) {
  auto buffer =
      device_->CreateBuffer(byte_size,
                            vk::BufferUsageFlagBits::eStorageBuffer |
                                vk::BufferUsageFlagBits::eTransferDst,
                            vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent);

  auto memory = device_->AllocateMemory(
      buffer, vk::MemoryPropertyFlagBits::eHostVisible |
                  vk::MemoryPropertyFlagBits::eHostCoherent);

  device_->UploadToBuffer(buffer, memory, data, byte_size);

  BufferInfo info;
  info.buffer = std::move(buffer);
  info.memory = std::move(memory);
  info.size = byte_size;
  buffers_[{set, binding}] = std::move(info);

  UpdateDescriptorSets();
}

void ComputeTestHarness::SetUniformBufferRaw(uint32_t binding, const void* data,
                                             size_t byte_size, uint32_t set) {
  auto buffer =
      device_->CreateBuffer(byte_size,
                            vk::BufferUsageFlagBits::eUniformBuffer |
                                vk::BufferUsageFlagBits::eTransferDst,
                            vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent);

  auto memory = device_->AllocateMemory(
      buffer, vk::MemoryPropertyFlagBits::eHostVisible |
                  vk::MemoryPropertyFlagBits::eHostCoherent);

  device_->UploadToBuffer(buffer, memory, data, byte_size);

  BufferInfo info;
  info.buffer = std::move(buffer);
  info.memory = std::move(memory);
  info.size = byte_size;
  info.is_uniform_buffer = true;
  buffers_[{set, binding}] = std::move(info);

  UpdateDescriptorSets();
}

void ComputeTestHarness::SetTexelBufferRaw(uint32_t binding, const void* data,
                                           size_t byte_size, vk::Format format,
                                           uint32_t set) {
  auto buffer =
      device_->CreateBuffer(byte_size,
                            vk::BufferUsageFlagBits::eUniformTexelBuffer |
                                vk::BufferUsageFlagBits::eTransferDst,
                            vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent);

  auto memory = device_->AllocateMemory(
      buffer, vk::MemoryPropertyFlagBits::eHostVisible |
                  vk::MemoryPropertyFlagBits::eHostCoherent);

  device_->UploadToBuffer(buffer, memory, data, byte_size);

  // Create buffer view for texel buffer
  vk::BufferViewCreateInfo view_info({}, *buffer, format, 0, byte_size);
  auto buffer_view = vk::raii::BufferView(device_->GetDevice(), view_info);

  BufferInfo info;
  info.buffer = std::move(buffer);
  info.memory = std::move(memory);
  info.buffer_view = std::move(buffer_view);
  info.size = byte_size;
  info.is_texel_buffer = true;
  buffers_[{set, binding}] = std::move(info);

  UpdateDescriptorSets();
}

void ComputeTestHarness::AllocateOutputBuffer(uint32_t binding,
                                              size_t byte_size, uint32_t set) {
  auto buffer =
      device_->CreateBuffer(byte_size,
                            vk::BufferUsageFlagBits::eStorageBuffer |
                                vk::BufferUsageFlagBits::eTransferSrc,
                            vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent);

  auto memory = device_->AllocateMemory(
      buffer, vk::MemoryPropertyFlagBits::eHostVisible |
                  vk::MemoryPropertyFlagBits::eHostCoherent);

  BufferInfo info;
  info.buffer = std::move(buffer);
  info.memory = std::move(memory);
  info.size = byte_size;
  buffers_[{set, binding}] = std::move(info);

  UpdateDescriptorSets();
}

void ComputeTestHarness::Dispatch(uint32_t group_x, uint32_t group_y,
                                  uint32_t group_z) {
  auto cmd = device_->BeginSingleTimeCommands();

  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline_);

  // Bind all descriptor sets
  std::vector<vk::DescriptorSet> sets;
  for (auto& ds : descriptor_sets_) {
    sets.push_back(*ds);
  }
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipeline_layout_, 0,
                         sets, {});

  // Set push constants if any
  if (!push_constant_data_.empty()) {
    cmd.pushConstants<uint8_t>(*pipeline_layout_,
                               vk::ShaderStageFlagBits::eCompute, 0,
                               push_constant_data_);
  }

  cmd.dispatch(group_x, group_y, group_z);

  device_->EndSingleTimeCommands(cmd);
}

bool ComputeTestHarness::CreateShaderModule(
    const std::vector<uint32_t>& spirv) {
  try {
    vk::ShaderModuleCreateInfo create_info({}, spirv.size() * sizeof(uint32_t),
                                           spirv.data());

    shader_module_ = vk::raii::ShaderModule(device_->GetDevice(), create_info);
    return true;
  } catch (const vk::SystemError& e) {
    XELOGE("Failed to create shader module: {}", e.what());
    return false;
  }
}

bool ComputeTestHarness::CreateDescriptorSetLayout() {
  try {
    // Will create layouts dynamically when resources are bound
    // For now, just return true
    return true;
  } catch (const vk::SystemError& e) {
    XELOGE("Failed to create descriptor set layout: {}", e.what());
    return false;
  }
}

void ComputeTestHarness::EnsureDescriptorSetLayouts() {
  // Determine how many sets we need
  uint32_t max_set = 0;
  for (const auto& [sb, _] : buffers_) {
    max_set = std::max(max_set, sb.set);
  }
  for (const auto& [sb, _] : images_) {
    max_set = std::max(max_set, sb.set);
  }

  // Create layouts for sets 0 to max_set
  descriptor_set_layouts_.clear();
  for (uint32_t set_idx = 0; set_idx <= max_set; ++set_idx) {
    std::vector<vk::DescriptorSetLayoutBinding> bindings;

    // Add buffer bindings for this set
    for (const auto& [sb, info] : buffers_) {
      if (sb.set == set_idx) {
        vk::DescriptorType type;
        if (info.is_texel_buffer) {
          type = vk::DescriptorType::eUniformTexelBuffer;
        } else if (info.is_uniform_buffer) {
          type = vk::DescriptorType::eUniformBuffer;
        } else {
          type = vk::DescriptorType::eStorageBuffer;
        }
        bindings.push_back(vk::DescriptorSetLayoutBinding(
            sb.binding, type, 1, vk::ShaderStageFlagBits::eCompute));
      }
    }

    // Add image bindings for this set
    for (const auto& [sb, info] : images_) {
      if (sb.set == set_idx) {
        vk::DescriptorType type =
            info.is_storage ? vk::DescriptorType::eStorageImage
                            : vk::DescriptorType::eCombinedImageSampler;
        bindings.push_back(vk::DescriptorSetLayoutBinding(
            sb.binding, type, 1, vk::ShaderStageFlagBits::eCompute));
      }
    }

    if (bindings.empty()) {
      // Empty set - add a dummy binding
      bindings.push_back(
          vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer,
                                         1, vk::ShaderStageFlagBits::eCompute));
    }

    vk::DescriptorSetLayoutCreateInfo layout_info({}, bindings);
    descriptor_set_layouts_.push_back(
        vk::raii::DescriptorSetLayout(device_->GetDevice(), layout_info));
  }
}

// Old functions removed - now handled dynamically in UpdateDescriptorSets()

void ComputeTestHarness::UpdateDescriptorSets() {
  // Recreate layouts and sets based on current resources
  EnsureDescriptorSetLayouts();

  // Recreate pipeline layout with new descriptor set layouts
  std::vector<vk::DescriptorSetLayout> layouts;
  for (auto& layout : descriptor_set_layouts_) {
    layouts.push_back(*layout);
  }

  vk::PushConstantRange push_constant_range(vk::ShaderStageFlagBits::eCompute,
                                            0, 128);
  vk::PipelineLayoutCreateInfo layout_info({}, layouts, push_constant_range);
  pipeline_layout_ =
      vk::raii::PipelineLayout(device_->GetDevice(), layout_info);

  // Recreate pipeline with new layout
  vk::PipelineShaderStageCreateInfo stage_info(
      {}, vk::ShaderStageFlagBits::eCompute, *shader_module_, "main");
  vk::ComputePipelineCreateInfo pipeline_info({}, stage_info,
                                              *pipeline_layout_);
  auto pipelines =
      vk::raii::Pipelines(device_->GetDevice(), nullptr, {pipeline_info});
  pipeline_ = std::move(pipelines[0]);

  // Recreate descriptor pool
  std::vector<vk::DescriptorPoolSize> pool_sizes = {
      {vk::DescriptorType::eStorageBuffer, 32},
      {vk::DescriptorType::eUniformBuffer, 32},
      {vk::DescriptorType::eUniformTexelBuffer, 32},
      {vk::DescriptorType::eCombinedImageSampler, 16},
      {vk::DescriptorType::eStorageImage, 16}};
  vk::DescriptorPoolCreateInfo pool_info({}, layouts.size(), pool_sizes);
  descriptor_pool_ = vk::raii::DescriptorPool(device_->GetDevice(), pool_info);

  // Allocate descriptor sets
  vk::DescriptorSetAllocateInfo alloc_info(*descriptor_pool_, layouts);
  descriptor_sets_.clear();
  auto sets = vk::raii::DescriptorSets(device_->GetDevice(), alloc_info);
  for (auto& set : sets) {
    descriptor_sets_.push_back(std::move(set));
  }

  // Update descriptor sets with resource bindings
  std::vector<vk::DescriptorBufferInfo> buffer_infos;
  std::vector<vk::DescriptorImageInfo> image_infos;
  std::vector<vk::BufferView> buffer_views;
  std::vector<vk::WriteDescriptorSet> writes;

  buffer_infos.reserve(buffers_.size());
  image_infos.reserve(images_.size());
  buffer_views.reserve(buffers_.size());

  for (const auto& [sb, info] : buffers_) {
    if (info.is_texel_buffer) {
      buffer_views.push_back(*info.buffer_view);
      writes.push_back(
          vk::WriteDescriptorSet(*descriptor_sets_[sb.set], sb.binding, 0,
                                 vk::DescriptorType::eUniformTexelBuffer, {},
                                 {}, buffer_views.back()));
    } else if (info.is_uniform_buffer) {
      buffer_infos.push_back(
          vk::DescriptorBufferInfo(*info.buffer, 0, info.size));
      writes.push_back(vk::WriteDescriptorSet(
          *descriptor_sets_[sb.set], sb.binding, 0,
          vk::DescriptorType::eUniformBuffer, {}, buffer_infos.back()));
    } else {
      buffer_infos.push_back(
          vk::DescriptorBufferInfo(*info.buffer, 0, info.size));
      writes.push_back(vk::WriteDescriptorSet(
          *descriptor_sets_[sb.set], sb.binding, 0,
          vk::DescriptorType::eStorageBuffer, {}, buffer_infos.back()));
    }
  }

  for (const auto& [sb, info] : images_) {
    if (info.is_storage) {
      image_infos.push_back(vk::DescriptorImageInfo(nullptr, *info.view,
                                                    vk::ImageLayout::eGeneral));
      writes.push_back(vk::WriteDescriptorSet(
          *descriptor_sets_[sb.set], sb.binding, 0,
          vk::DescriptorType::eStorageImage, image_infos.back(), {}));
    } else {
      image_infos.push_back(vk::DescriptorImageInfo(
          *info.sampler, *info.view, vk::ImageLayout::eShaderReadOnlyOptimal));
      writes.push_back(vk::WriteDescriptorSet(
          *descriptor_sets_[sb.set], sb.binding, 0,
          vk::DescriptorType::eCombinedImageSampler, image_infos.back(), {}));
    }
  }

  if (!writes.empty()) {
    device_->GetDevice().updateDescriptorSets(writes, {});
  }
}

size_t ComputeTestHarness::GetFormatSize(vk::Format format) const {
  switch (format) {
    case vk::Format::eR8G8B8A8Unorm:
    case vk::Format::eB8G8R8A8Unorm:
      return 4;
    case vk::Format::eR16G16B16A16Sfloat:
      return 8;
    case vk::Format::eR32G32B32A32Sfloat:
      return 16;
    default:
      return 4;
  }
}

void ComputeTestHarness::SetTexture2DRaw(uint32_t binding, uint32_t width,
                                         uint32_t height, vk::Format format,
                                         const void* data, size_t byte_size,
                                         uint32_t set) {
  auto image = device_->CreateImage(
      width, height, format, vk::ImageTiling::eOptimal,
      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal);

  auto memory =
      device_->AllocateMemory(image, vk::MemoryPropertyFlagBits::eDeviceLocal);

  device_->UploadToImage(image, width, height, format, data, byte_size);

  auto view =
      device_->CreateImageView(image, format, vk::ImageAspectFlagBits::eColor);
  auto sampler = device_->CreateSampler();

  ImageInfo info;
  info.image = std::move(image);
  info.memory = std::move(memory);
  info.view = std::move(view);
  info.sampler = std::move(sampler);
  info.width = width;
  info.height = height;
  info.format = format;
  info.is_storage = false;
  images_[{set, binding}] = std::move(info);

  UpdateDescriptorSets();
}

void ComputeTestHarness::AllocateOutputImage2D(uint32_t binding, uint32_t width,
                                               uint32_t height,
                                               vk::Format format,
                                               uint32_t set) {
  auto image = device_->CreateImage(
      width, height, format, vk::ImageTiling::eOptimal,
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eDeviceLocal);

  auto memory =
      device_->AllocateMemory(image, vk::MemoryPropertyFlagBits::eDeviceLocal);

  // Transition image to eGeneral layout for storage writes
  auto cmd = device_->BeginSingleTimeCommands();
  vk::ImageMemoryBarrier barrier(
      {}, vk::AccessFlagBits::eShaderWrite, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eGeneral, VK_QUEUE_FAMILY_IGNORED,
      VK_QUEUE_FAMILY_IGNORED, *image,
      vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                      vk::PipelineStageFlagBits::eComputeShader, {}, {}, {},
                      barrier);
  device_->EndSingleTimeCommands(cmd);

  auto view =
      device_->CreateImageView(image, format, vk::ImageAspectFlagBits::eColor);

  ImageInfo info;
  info.image = std::move(image);
  info.memory = std::move(memory);
  info.view = std::move(view);
  info.sampler = nullptr;  // Storage images don't need samplers
  info.width = width;
  info.height = height;
  info.format = format;
  info.is_storage = true;
  images_[{set, binding}] = std::move(info);

  UpdateDescriptorSets();
}

void ComputeTestHarness::SetPushConstantsRaw(const void* data,
                                             size_t byte_size) {
  push_constant_data_.resize(byte_size);
  std::memcpy(push_constant_data_.data(), data, byte_size);
}

}  // namespace testing
}  // namespace shaders
}  // namespace gpu
}  // namespace xe
