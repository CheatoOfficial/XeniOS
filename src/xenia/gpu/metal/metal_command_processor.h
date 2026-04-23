/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_COMMAND_PROCESSOR_H_
#define XENIA_GPU_METAL_METAL_COMMAND_PROCESSOR_H_

#include "xenia/gpu/command_processor.h"
#include "xenia/ui/metal/metal_provider.h"

namespace xe {
namespace gpu {
namespace metal {

class MetalCommandProcessor : public CommandProcessor {
 public:
  MetalCommandProcessor(GraphicsSystem* graphics_system,
                        kernel::KernelState* kernel_state);
  ~MetalCommandProcessor() override;

  ui::metal::MetalProvider& GetMetalProvider() const;
  MTL::Device* GetMetalDevice() const { return GetMetalProvider().GetDevice(); }

  void TracePlaybackWroteMemory(uint32_t base_ptr, uint32_t length) override;
  void RestoreEdramSnapshot(const void* snapshot) override;

 protected:
  bool SetupContext() override;
  void ShutdownContext() override;
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_COMMAND_PROCESSOR_H_
