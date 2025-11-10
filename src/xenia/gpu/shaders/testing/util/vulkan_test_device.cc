/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/shaders/testing/util/vulkan_test_device.h"

#include <cstring>
#include "xenia/base/logging.h"
#include "xenia/gpu/shaders/testing/util/depth_pattern_shaders.h"

namespace xe {
namespace gpu {
namespace shaders {
namespace testing {

VulkanTestDevice::VulkanTestDevice() = default;

bool VulkanTestDevice::Initialize() {
  if (!CreateInstance()) {
    return false;
  }
  if (!SelectPhysicalDevice()) {
    return false;
  }
  if (!CreateDevice()) {
    return false;
  }
  if (!CreateCommandPool()) {
    return false;
  }
  return true;
}

bool VulkanTestDevice::CreateInstance() {
  try {
    vk::ApplicationInfo app_info("XeniaShaderTest", 1, "Xenia", 1,
                                 VK_API_VERSION_1_0);

    vk::InstanceCreateInfo create_info({}, &app_info);

    instance_ = std::make_unique<vk::raii::Instance>(context_, create_info);
    return true;
  } catch (const vk::SystemError& e) {
    XELOGE("Failed to create Vulkan instance: {}", e.what());
    return false;
  }
}

bool VulkanTestDevice::SelectPhysicalDevice() {
  try {
    auto physical_devices = instance_->enumeratePhysicalDevices();
    if (physical_devices.empty()) {
      XELOGE("No Vulkan physical devices found");
      return false;
    }

    // Prefer discrete GPU, fallback to any
    for (auto& pd : physical_devices) {
      auto props = pd.getProperties();
      if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
        physical_device_ =
            std::make_unique<vk::raii::PhysicalDevice>(std::move(pd));
        XELOGI("Selected discrete GPU: {}", props.deviceName.data());
        return true;
      }
    }

    // Use first device as fallback
    physical_device_ = std::make_unique<vk::raii::PhysicalDevice>(
        std::move(physical_devices[0]));
    auto props = physical_device_->getProperties();
    XELOGI("Selected device: {}", props.deviceName.data());
    return true;
  } catch (const vk::SystemError& e) {
    XELOGE("Failed to select physical device: {}", e.what());
    return false;
  }
}

bool VulkanTestDevice::CreateDevice() {
  try {
    // Find compute queue family
    auto queue_families = physical_device_->getQueueFamilyProperties();
    for (uint32_t i = 0; i < queue_families.size(); ++i) {
      if (queue_families[i].queueFlags & vk::QueueFlagBits::eCompute) {
        queue_family_index_ = i;
        break;
      }
    }

    float queue_priority = 1.0f;
    vk::DeviceQueueCreateInfo queue_create_info({}, queue_family_index_, 1,
                                                &queue_priority);

    vk::DeviceCreateInfo device_create_info({}, queue_create_info);

    device_ = std::make_unique<vk::raii::Device>(*physical_device_,
                                                 device_create_info);

    queue_ = std::make_unique<vk::raii::Queue>(
        device_->getQueue(queue_family_index_, 0));

    return true;
  } catch (const vk::SystemError& e) {
    XELOGE("Failed to create device: {}", e.what());
    return false;
  }
}

bool VulkanTestDevice::CreateCommandPool() {
  try {
    vk::CommandPoolCreateInfo pool_info(
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        queue_family_index_);

    command_pool_ =
        std::make_unique<vk::raii::CommandPool>(*device_, pool_info);

    return true;
  } catch (const vk::SystemError& e) {
    XELOGE("Failed to create command pool: {}", e.what());
    return false;
  }
}

uint32_t VulkanTestDevice::FindMemoryType(uint32_t type_filter,
                                          vk::MemoryPropertyFlags properties) {
  auto mem_properties = physical_device_->getMemoryProperties();

  for (uint32_t i = 0; i < mem_properties.memoryTypeCount; ++i) {
    if ((type_filter & (1 << i)) &&
        (mem_properties.memoryTypes[i].propertyFlags & properties) ==
            properties) {
      return i;
    }
  }

  throw std::runtime_error("Failed to find suitable memory type");
}

vk::raii::Buffer VulkanTestDevice::CreateBuffer(
    vk::DeviceSize size, vk::BufferUsageFlags usage,
    vk::MemoryPropertyFlags mem_props) {
  vk::BufferCreateInfo buffer_info({}, size, usage);
  return vk::raii::Buffer(*device_, buffer_info);
}

vk::raii::DeviceMemory VulkanTestDevice::AllocateMemory(
    const vk::raii::Buffer& buffer, vk::MemoryPropertyFlags mem_props) {
  auto mem_requirements = buffer.getMemoryRequirements();
  vk::MemoryAllocateInfo alloc_info(
      mem_requirements.size,
      FindMemoryType(mem_requirements.memoryTypeBits, mem_props));

  auto memory = vk::raii::DeviceMemory(*device_, alloc_info);
  buffer.bindMemory(*memory, 0);
  return memory;
}

void* VulkanTestDevice::Map(const vk::raii::DeviceMemory& memory,
                            vk::DeviceSize size) {
  return memory.mapMemory(0, size);
}

void VulkanTestDevice::Unmap(const vk::raii::DeviceMemory& memory) {
  memory.unmapMemory();
}

vk::raii::Image VulkanTestDevice::CreateImage(uint32_t width, uint32_t height,
                                              vk::Format format,
                                              vk::ImageTiling tiling,
                                              vk::ImageUsageFlags usage,
                                              vk::MemoryPropertyFlags mem_props,
                                              vk::SampleCountFlagBits samples) {
  vk::ImageCreateInfo image_info({}, vk::ImageType::e2D, format,
                                 vk::Extent3D(width, height, 1), 1, 1, samples,
                                 tiling, usage);

  return vk::raii::Image(*device_, image_info);
}

vk::raii::DeviceMemory VulkanTestDevice::AllocateMemory(
    const vk::raii::Image& image, vk::MemoryPropertyFlags mem_props) {
  auto mem_requirements = image.getMemoryRequirements();
  vk::MemoryAllocateInfo alloc_info(
      mem_requirements.size,
      FindMemoryType(mem_requirements.memoryTypeBits, mem_props));

  auto memory = vk::raii::DeviceMemory(*device_, alloc_info);
  image.bindMemory(*memory, 0);
  return memory;
}

vk::raii::ImageView VulkanTestDevice::CreateImageView(
    const vk::raii::Image& image, vk::Format format,
    vk::ImageAspectFlags aspect_flags, vk::SampleCountFlagBits samples) {
  // MSAA textures always use e2D view type (samples are part of image, not
  // array layers)
  vk::ImageViewCreateInfo view_info(
      {}, *image, vk::ImageViewType::e2D, format, {},
      vk::ImageSubresourceRange(aspect_flags, 0, 1, 0, 1));

  return vk::raii::ImageView(*device_, view_info);
}

vk::raii::Sampler VulkanTestDevice::CreateSampler() {
  vk::SamplerCreateInfo sampler_info(
      {}, vk::Filter::eLinear, vk::Filter::eLinear,
      vk::SamplerMipmapMode::eLinear, vk::SamplerAddressMode::eClampToEdge,
      vk::SamplerAddressMode::eClampToEdge,
      vk::SamplerAddressMode::eClampToEdge, 0.0f, VK_FALSE, 1.0f, VK_FALSE,
      vk::CompareOp::eAlways, 0.0f, 0.0f, vk::BorderColor::eIntOpaqueBlack,
      VK_FALSE);

  return vk::raii::Sampler(*device_, sampler_info);
}

vk::raii::CommandBuffer VulkanTestDevice::BeginSingleTimeCommands() {
  vk::CommandBufferAllocateInfo alloc_info(*command_pool_,
                                           vk::CommandBufferLevel::ePrimary, 1);

  auto cmd_buffers = vk::raii::CommandBuffers(*device_, alloc_info);
  auto cmd = std::move(cmd_buffers[0]);

  vk::CommandBufferBeginInfo begin_info(
      vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
  cmd.begin(begin_info);

  return cmd;
}

void VulkanTestDevice::EndSingleTimeCommands(vk::raii::CommandBuffer& cmd) {
  cmd.end();

  vk::SubmitInfo submit_info({}, {}, *cmd);
  queue_->submit(submit_info);
  queue_->waitIdle();
}

void VulkanTestDevice::UploadToBuffer(const vk::raii::Buffer& buffer,
                                      const vk::raii::DeviceMemory& memory,
                                      const void* data, size_t size) {
  void* mapped = Map(memory, size);
  std::memcpy(mapped, data, size);
  Unmap(memory);
}

void VulkanTestDevice::DownloadFromBuffer(const vk::raii::Buffer& buffer,
                                          const vk::raii::DeviceMemory& memory,
                                          void* data, size_t size) {
  void* mapped = Map(memory, size);
  std::memcpy(data, mapped, size);
  Unmap(memory);
}

void VulkanTestDevice::UploadToImage(const vk::raii::Image& image,
                                     uint32_t width, uint32_t height,
                                     vk::Format format, const void* data,
                                     size_t size) {
  // Create staging buffer
  auto staging_buffer =
      CreateBuffer(size, vk::BufferUsageFlagBits::eTransferSrc,
                   vk::MemoryPropertyFlagBits::eHostVisible |
                       vk::MemoryPropertyFlagBits::eHostCoherent);
  auto staging_memory = AllocateMemory(
      staging_buffer, vk::MemoryPropertyFlagBits::eHostVisible |
                          vk::MemoryPropertyFlagBits::eHostCoherent);

  UploadToBuffer(staging_buffer, staging_memory, data, size);

  auto cmd = BeginSingleTimeCommands();

  // Transition to transfer dst
  vk::ImageMemoryBarrier barrier(
      {}, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eTransferDstOptimal, VK_QUEUE_FAMILY_IGNORED,
      VK_QUEUE_FAMILY_IGNORED, *image,
      vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                      vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
                      barrier);

  // Copy buffer to image
  vk::BufferImageCopy region(
      0, 0, 0,
      vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
      {0, 0, 0}, {width, height, 1});

  cmd.copyBufferToImage(*staging_buffer, *image,
                        vk::ImageLayout::eTransferDstOptimal, region);

  // Transition to shader read
  barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
  barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
  barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
  barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                      vk::PipelineStageFlagBits::eComputeShader, {}, {}, {},
                      barrier);

  EndSingleTimeCommands(cmd);
}

void VulkanTestDevice::DownloadFromImage(const vk::raii::Image& image,
                                         uint32_t width, uint32_t height,
                                         vk::Format format, void* data,
                                         size_t size) {
  // Create staging buffer
  auto staging_buffer =
      CreateBuffer(size, vk::BufferUsageFlagBits::eTransferDst,
                   vk::MemoryPropertyFlagBits::eHostVisible |
                       vk::MemoryPropertyFlagBits::eHostCoherent);
  auto staging_memory = AllocateMemory(
      staging_buffer, vk::MemoryPropertyFlagBits::eHostVisible |
                          vk::MemoryPropertyFlagBits::eHostCoherent);

  auto cmd = BeginSingleTimeCommands();

  // Transition to transfer src
  vk::ImageMemoryBarrier barrier(
      vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead,
      vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal,
      VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, *image,
      vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
                      barrier);

  // Copy image to buffer
  vk::BufferImageCopy region(
      0, 0, 0,
      vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
      {0, 0, 0}, {width, height, 1});

  cmd.copyImageToBuffer(*image, vk::ImageLayout::eTransferSrcOptimal,
                        *staging_buffer, region);

  EndSingleTimeCommands(cmd);

  DownloadFromBuffer(staging_buffer, staging_memory, data, size);
}

vk::raii::ShaderModule VulkanTestDevice::CreateShaderModule(
    const std::vector<uint32_t>& spirv) {
  vk::ShaderModuleCreateInfo create_info({}, spirv.size() * sizeof(uint32_t),
                                         spirv.data());
  return vk::raii::ShaderModule(*device_, create_info);
}

vk::raii::RenderPass VulkanTestDevice::CreateRenderPass(
    vk::Format depth_format, vk::SampleCountFlagBits samples) {
  vk::AttachmentDescription depth_attachment(
      {}, depth_format, samples, vk::AttachmentLoadOp::eClear,
      vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare,
      vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eDepthStencilAttachmentOptimal);

  vk::AttachmentReference depth_ref(
      0, vk::ImageLayout::eDepthStencilAttachmentOptimal);

  vk::SubpassDescription subpass;
  subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
  subpass.pDepthStencilAttachment = &depth_ref;

  vk::RenderPassCreateInfo create_info({}, depth_attachment, subpass);
  return vk::raii::RenderPass(*device_, create_info);
}

vk::raii::Framebuffer VulkanTestDevice::CreateFramebuffer(
    const vk::raii::RenderPass& render_pass,
    const vk::raii::ImageView& depth_view, uint32_t width, uint32_t height) {
  vk::FramebufferCreateInfo create_info({}, *render_pass, *depth_view, width,
                                        height, 1);
  return vk::raii::Framebuffer(*device_, create_info);
}

void VulkanTestDevice::RenderDepthPattern(const vk::raii::Image& depth_image,
                                          const vk::raii::ImageView& depth_view,
                                          uint32_t width, uint32_t height,
                                          vk::Format depth_format,
                                          vk::SampleCountFlagBits samples) {
  // Use precompiled GLSL shaders
  std::vector<uint32_t> vert_spirv(
      depth_pattern_vert_spv,
      depth_pattern_vert_spv +
          sizeof(depth_pattern_vert_spv) / sizeof(uint32_t));
  std::vector<uint32_t> frag_spirv(
      depth_pattern_frag_spv,
      depth_pattern_frag_spv +
          sizeof(depth_pattern_frag_spv) / sizeof(uint32_t));

  auto vert_module = CreateShaderModule(vert_spirv);
  auto frag_module = CreateShaderModule(frag_spirv);

  auto render_pass = CreateRenderPass(depth_format, samples);
  auto framebuffer = CreateFramebuffer(render_pass, depth_view, width, height);

  // Create pipeline layout (no descriptors needed)
  vk::PipelineLayoutCreateInfo layout_info;
  auto pipeline_layout = vk::raii::PipelineLayout(*device_, layout_info);

  // Shader stages
  vk::PipelineShaderStageCreateInfo vert_stage(
      {}, vk::ShaderStageFlagBits::eVertex, *vert_module, "main");
  vk::PipelineShaderStageCreateInfo frag_stage(
      {}, vk::ShaderStageFlagBits::eFragment, *frag_module, "main");
  std::vector<vk::PipelineShaderStageCreateInfo> stages = {vert_stage,
                                                           frag_stage};

  // Vertex input (none - generated in shader)
  vk::PipelineVertexInputStateCreateInfo vertex_input;

  // Input assembly
  vk::PipelineInputAssemblyStateCreateInfo input_assembly(
      {}, vk::PrimitiveTopology::eTriangleList);

  // Viewport
  vk::Viewport viewport(0, 0, static_cast<float>(width),
                        static_cast<float>(height), 0.0f, 1.0f);
  vk::Rect2D scissor({0, 0}, {width, height});
  vk::PipelineViewportStateCreateInfo viewport_state({}, viewport, scissor);

  // Rasterization
  vk::PipelineRasterizationStateCreateInfo rasterization(
      {}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone,
      vk::FrontFace::eCounterClockwise, false, 0.0f, 0.0f, 0.0f, 1.0f);

  // Multisample
  vk::PipelineMultisampleStateCreateInfo multisample({}, samples);

  // Depth stencil
  vk::PipelineDepthStencilStateCreateInfo depth_stencil({}, true, true,
                                                        vk::CompareOp::eAlways);

  // Color blend (no color attachments)
  vk::PipelineColorBlendStateCreateInfo color_blend;

  // Create pipeline
  vk::GraphicsPipelineCreateInfo pipeline_info(
      {}, stages, &vertex_input, &input_assembly, {}, &viewport_state,
      &rasterization, &multisample, &depth_stencil, &color_blend, {},
      *pipeline_layout, *render_pass);

  auto pipeline = vk::raii::Pipeline(*device_, nullptr, pipeline_info);

  // Render
  auto cmd = BeginSingleTimeCommands();

  // Transition image to depth attachment
  vk::ImageMemoryBarrier barrier(
      {}, vk::AccessFlagBits::eDepthStencilAttachmentWrite,
      vk::ImageLayout::eUndefined,
      vk::ImageLayout::eDepthStencilAttachmentOptimal, VK_QUEUE_FAMILY_IGNORED,
      VK_QUEUE_FAMILY_IGNORED, *depth_image,
      vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1));

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                      vk::PipelineStageFlagBits::eEarlyFragmentTests, {}, {},
                      {}, barrier);

  // Begin render pass
  vk::ClearValue clear_value;
  clear_value.depthStencil.depth = 0.0f;
  clear_value.depthStencil.stencil = 0;
  vk::RenderPassBeginInfo render_pass_info(*render_pass, *framebuffer,
                                           vk::Rect2D({0, 0}, {width, height}),
                                           clear_value);

  cmd.beginRenderPass(render_pass_info, vk::SubpassContents::eInline);
  cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
  cmd.draw(3, 1, 0, 0);  // Draw fullscreen triangle
  cmd.endRenderPass();

  // Transition to shader read
  barrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
  barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
  barrier.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
  barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eLateFragmentTests,
                      vk::PipelineStageFlagBits::eComputeShader, {}, {}, {},
                      barrier);

  EndSingleTimeCommands(cmd);
}

}  // namespace testing
}  // namespace shaders
}  // namespace gpu
}  // namespace xe
