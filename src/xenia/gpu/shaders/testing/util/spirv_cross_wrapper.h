/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SHADERS_TESTING_UTIL_SPIRV_CROSS_WRAPPER_H_
#define XENIA_GPU_SHADERS_TESTING_UTIL_SPIRV_CROSS_WRAPPER_H_

#include <cstdint>
#include <string>
#include <vector>

namespace xe {
namespace gpu {
namespace shaders {
namespace testing {

// Wrapper for SPIRV-Cross shader conversion and validation.
// Provides DXBC/DXIL to SPIR-V conversion and SPIR-V validation.
class SPIRVCrossWrapper {
 public:
  // Convert DXIL bytecode to SPIR-V using SPIRV-Cross
  static std::vector<uint32_t> DXILToSPIRV(const std::vector<uint8_t>& dxil);

  // Convert DXBC bytecode to SPIR-V using SPIRV-Cross
  static std::vector<uint32_t> DXBCToSPIRV(const std::vector<uint8_t>& dxbc);

  // Validate SPIR-V bytecode using spirv-val
  // Returns true if valid, false otherwise
  // If error is not null, it will be filled with validation errors
  static bool ValidateSPIRV(const std::vector<uint32_t>& spirv,
                            std::string* error = nullptr);

  // Disassemble SPIR-V to human-readable text (for debugging)
  static std::string DisassembleSPIRV(const std::vector<uint32_t>& spirv);

  // Descriptor binding information
  struct DescriptorBinding {
    uint32_t set;
    uint32_t binding;
    std::string name;
    enum Type {
      UNIFORM_BUFFER,
      STORAGE_BUFFER,
      SAMPLED_IMAGE,
      STORAGE_IMAGE,
      UNKNOWN
    } type;
  };

  // Reflect on SPIR-V to get descriptor bindings
  static std::vector<DescriptorBinding> ReflectDescriptorBindings(
      const std::vector<uint32_t>& spirv);

  // Get last error message
  static const std::string& GetLastError();

 private:
  static thread_local std::string last_error_;
};

}  // namespace testing
}  // namespace shaders
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SHADERS_TESTING_UTIL_SPIRV_CROSS_WRAPPER_H_
