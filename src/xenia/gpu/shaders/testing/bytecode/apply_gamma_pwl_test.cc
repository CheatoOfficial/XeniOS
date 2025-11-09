/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "third_party/catch/include/catch.hpp"
#include "xenia/gpu/shaders/testing/util/compute_test_harness.h"
#include "xenia/gpu/shaders/testing/util/spirv_cross_wrapper.h"
#include "xenia/gpu/shaders/testing/util/vulkan_test_device.h"

#include <cstdint>

// Define BYTE for D3D12 headers
#ifndef BYTE
typedef uint8_t BYTE;
#endif

// Include DXBC bytecode
#define apply_gamma_pwl_cs apply_gamma_pwl_cs_dxbc
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/apply_gamma_pwl_cs.h"
#undef apply_gamma_pwl_cs

#define apply_gamma_pwl_fxaa_luma_cs apply_gamma_pwl_fxaa_luma_cs_dxbc
#include "xenia/gpu/shaders/bytecode/d3d12_5_1/apply_gamma_pwl_fxaa_luma_cs.h"
#undef apply_gamma_pwl_fxaa_luma_cs

// Include SPIR-V bytecode
#define apply_gamma_pwl_cs apply_gamma_pwl_cs_spirv
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/apply_gamma_pwl_cs.h"
#undef apply_gamma_pwl_cs

#define apply_gamma_pwl_fxaa_luma_cs apply_gamma_pwl_fxaa_luma_cs_spirv
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/apply_gamma_pwl_fxaa_luma_cs.h"
#undef apply_gamma_pwl_fxaa_luma_cs

using namespace xe::gpu::shaders::testing;

// Tests for the production apply_gamma_pwl compute shader
// Runs both DXBC-converted and native SPIR-V versions

// Helper function to calculate expected PWL gamma output
// Formula: output = (base + (multiplier * delta) / 8) / (64 * 1023)
// Where multiplier = input_value & 7 (lower 3 bits)
static float CalculateExpectedPWLGamma(uint32_t input_value, uint32_t base,
                                       uint32_t delta) {
  uint32_t multiplier = input_value & 7u;
  float result = (float(base) + float(multiplier * delta) * (1.0f / 8.0f)) *
                 (1.0f / (64.0f * 1023.0f));
  return std::min(1.0f, result);  // saturate
}

// Helper to convert half-float (FP16) to float
static float Half2Float(uint16_t h) {
  uint32_t sign = (h & 0x8000) << 16;
  uint32_t exp = (h & 0x7C00) >> 10;
  uint32_t frac = (h & 0x03FF);

  if (exp == 0) {
    if (frac == 0) return 0.0f;  // Zero
    // Denormalized
    exp = 1;
    while ((frac & 0x0400) == 0) {
      frac <<= 1;
      exp--;
    }
    frac &= 0x03FF;
  } else if (exp == 31) {
    // Inf or NaN
    return (frac == 0) ? INFINITY : NAN;
  }

  uint32_t f = sign | ((exp + 112) << 23) | (frac << 13);
  float result;
  std::memcpy(&result, &f, sizeof(float));
  return result;
}

TEST_CASE("apply_gamma_pwl: DXBC-converted production shader",
          "[shader][apply_gamma_pwl][cross_backend][dxbc]") {
  VulkanTestDevice device;
  if (!device.Initialize()) {
    WARN("Vulkan not available, skipping test");
    return;
  }

  // Load DXBC and convert to SPIR-V
  std::vector<uint8_t> dxbc_bytes(
      apply_gamma_pwl_cs_dxbc,
      apply_gamma_pwl_cs_dxbc + sizeof(apply_gamma_pwl_cs_dxbc));

  std::vector<uint32_t> spirv = SPIRVCrossWrapper::DXBCToSPIRV(dxbc_bytes);

  if (spirv.empty()) {
    FAIL("DXBC to SPIR-V conversion failed: "
         << SPIRVCrossWrapper::GetLastError());
  }

  // Use SPIRV reflection to detect descriptor bindings
  auto bindings = SPIRVCrossWrapper::ReflectDescriptorBindings(spirv);

  uint32_t gamma_ramp_set = 0, gamma_ramp_binding = 0;
  uint32_t source_tex_set = 0, source_tex_binding = 0;
  uint32_t output_img_set = 0, output_img_binding = 0;

  for (const auto& binding : bindings) {
    // Map resources based on name
    if (binding.name == "t0") {  // gamma ramp
      gamma_ramp_set = binding.set;
      gamma_ramp_binding = binding.binding;
    } else if (binding.name == "t1") {  // source texture
      source_tex_set = binding.set;
      source_tex_binding = binding.binding;
    } else if (binding.name == "u0") {  // output image
      output_img_set = binding.set;
      output_img_binding = binding.binding;
    }
  }

  // Create compute pipeline from converted SPIR-V
  ComputeTestHarness harness(&device, spirv);
  REQUIRE(harness.IsValid());

  // Set up resources for production shader
  const uint32_t width = 4;
  const uint32_t height = 2;

  // Push constants: dimensions
  struct PushConstants {
    uint32_t width;
    uint32_t height;
  } push_const = {width, height};
  harness.SetPushConstants(push_const);

  // DXBC converter puts push constants into cb1_struct uniform buffer
  // Bind the uniform buffer with the same data
  harness.SetUniformBuffer(0, push_const, 0);  // set=0, binding=0

  // Create PWL gamma ramp - use a known test ramp
  // Each entry has {base, delta} pair stored as .rg in the buffer
  // For testing: create a simple linear ramp where output = input
  std::vector<uint32_t> gamma_ramp(128 * 3 *
                                   2);  // 128 entries, 3 RGB, 2 values each
  for (uint32_t i = 0; i < 128; ++i) {
    // R, G, B entries (3 per ramp index)
    for (uint32_t c = 0; c < 3; ++c) {
      uint32_t entry_idx = (i * 3 + c) * 2;
      // base = i * 8 (shifted left 6 bits as per spec)
      // delta = 8 (shifted left 6 bits)
      // This creates a linear 1:1 mapping
      gamma_ramp[entry_idx] = (i * 8) << 6;  // base (ramp_value.x)
      gamma_ramp[entry_idx + 1] = 8 << 6;    // delta (ramp_value.y)
    }
  }
  // Bind resources using detected bindings from reflection
  // Use texel buffer for gamma ramp (shader expects texelFetch)
  harness.SetTexelBuffer(gamma_ramp_binding, gamma_ramp,
                         vk::Format::eR32G32Uint, gamma_ramp_set);

  // Create source texture with known test values
  // Using values that align with 8-step increments for easier validation
  std::vector<float> source_tex(width * height * 4);
  uint32_t test_values[] = {0,   256, 512, 768,
                            128, 384, 640, 896};  // 0-1023 range

  // Fill texture in row-major order (y * width + x)
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      uint32_t pixel_idx = y * width + x;
      uint32_t idx = pixel_idx * 4;
      // Convert test value back to 0-1 range for texture input
      source_tex[idx + 0] = float(test_values[pixel_idx]) / 1023.0f;  // R
      source_tex[idx + 1] = float(test_values[pixel_idx]) / 1023.0f;  // G
      source_tex[idx + 2] = float(test_values[pixel_idx]) / 1023.0f;  // B
      source_tex[idx + 3] = 1.0f;                                     // A
    }
  }
  harness.SetTexture2D(source_tex_binding, width, height,
                       vk::Format::eR32G32B32A32Sfloat, source_tex,
                       source_tex_set);

  // Allocate output storage image
  harness.AllocateOutputImage2D(output_img_binding, width, height,
                                vk::Format::eR16G16B16A16Sfloat,
                                output_img_set);

  // Dispatch compute shader
  harness.Dispatch(1, 1, 1);

  // Read back output as floats (R16G16B16A16_SFLOAT)
  auto output =
      harness.ReadOutputImage2D<uint16_t>(output_img_binding, output_img_set);
  REQUIRE(output.size() == width * height * 4);

  // Validate PWL gamma correction output
  for (uint32_t i = 0; i < width * height; ++i) {
    uint32_t input_val = test_values[i];
    uint32_t ramp_index = input_val >> 3;  // Divide by 8 (increment)

    // Get the ramp entry for this input (all RGB channels)
    for (uint32_t c = 0; c < 3; ++c) {  // R, G, B (skip A)
      uint32_t base = gamma_ramp[(ramp_index * 3 + c) * 2];
      uint32_t delta = gamma_ramp[(ramp_index * 3 + c) * 2 + 1];
      float expected = CalculateExpectedPWLGamma(input_val, base, delta);

      // Read output value (RGBA, 4 channels per pixel)
      uint16_t output_half = output[i * 4 + c];
      float actual = Half2Float(output_half);

      // Validate (allow small tolerance for FP16 precision)
      REQUIRE(actual == Approx(expected).margin(0.001f));
    }
  }
}

TEST_CASE("apply_gamma_pwl: Native SPIR-V production shader",
          "[shader][apply_gamma_pwl][cross_backend][spirv]") {
  VulkanTestDevice device;
  if (!device.Initialize()) {
    WARN("Vulkan not available, skipping test");
    return;
  }

  // Load native SPIR-V
  std::vector<uint32_t> spirv(
      apply_gamma_pwl_cs_spirv,
      apply_gamma_pwl_cs_spirv +
          sizeof(apply_gamma_pwl_cs_spirv) / sizeof(uint32_t));

  REQUIRE(SPIRVCrossWrapper::ValidateSPIRV(spirv));

  // Create compute pipeline from native SPIR-V
  ComputeTestHarness harness(&device, spirv);
  REQUIRE(harness.IsValid());

  // Set up resources - same as DXBC test for consistency
  const uint32_t width = 4;
  const uint32_t height = 2;

  // Push constants: dimensions
  struct PushConstants {
    uint32_t width;
    uint32_t height;
  } push_const = {width, height};
  harness.SetPushConstants(push_const);

  // Create PWL gamma ramp - same linear 1:1 mapping
  std::vector<uint32_t> gamma_ramp(128 * 3 * 2);
  for (uint32_t i = 0; i < 128; ++i) {
    for (uint32_t c = 0; c < 3; ++c) {
      uint32_t entry_idx = (i * 3 + c) * 2;
      gamma_ramp[entry_idx] = (i * 8) << 6;
      gamma_ramp[entry_idx + 1] = 8 << 6;
    }
  }
  // Bind gamma ramp as texel buffer (for texelFetch in shader)
  harness.SetTexelBuffer(0, gamma_ramp, vk::Format::eR32G32Uint,
                         0);  // set=0, binding=0

  // Create source texture with same known test values
  std::vector<float> source_tex(width * height * 4);
  uint32_t test_values[] = {0, 256, 512, 768, 128, 384, 640, 896};

  // Fill texture in row-major order (y * width + x)
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      uint32_t pixel_idx = y * width + x;
      uint32_t idx = pixel_idx * 4;
      source_tex[idx + 0] = float(test_values[pixel_idx]) / 1023.0f;
      source_tex[idx + 1] = float(test_values[pixel_idx]) / 1023.0f;
      source_tex[idx + 2] = float(test_values[pixel_idx]) / 1023.0f;
      source_tex[idx + 3] = 1.0f;
    }
  }
  harness.SetTexture2D(0, width, height, vk::Format::eR32G32B32A32Sfloat,
                       source_tex, 1);  // set=1, binding=0

  // Allocate output storage image
  harness.AllocateOutputImage2D(0, width, height,
                                vk::Format::eR16G16B16A16Sfloat,
                                2);  // set=2, binding=0

  // Dispatch compute shader
  harness.Dispatch(1, 1, 1);

  // Read back output
  auto output = harness.ReadOutputImage2D<uint16_t>(0, 2);  // binding=0, set=2
  REQUIRE(output.size() == width * height * 4);

  // Validate PWL gamma correction output
  for (uint32_t i = 0; i < width * height; ++i) {
    uint32_t input_val = test_values[i];
    uint32_t ramp_index = input_val >> 3;

    // Get the ramp entry for this input (all RGB channels)
    for (uint32_t c = 0; c < 3; ++c) {  // R, G, B (skip A)
      uint32_t base = gamma_ramp[(ramp_index * 3 + c) * 2];
      uint32_t delta = gamma_ramp[(ramp_index * 3 + c) * 2 + 1];
      float expected = CalculateExpectedPWLGamma(input_val, base, delta);

      // Read output value (RGBA, 4 channels per pixel)
      uint16_t output_half = output[i * 4 + c];
      float actual = Half2Float(output_half);

      // Validate (allow small tolerance for FP16 precision)
      REQUIRE(actual == Approx(expected).margin(0.001f));
    }
  }

  // Verify alpha channel is always 1.0 (not FXAA luma variant)
  for (uint32_t i = 0; i < width * height; ++i) {
    uint16_t alpha_half = output[i * 4 + 3];
    float alpha = Half2Float(alpha_half);
    REQUIRE(alpha == Approx(1.0f).margin(0.001f));
  }
}

TEST_CASE("apply_gamma_pwl: FXAA luma variant",
          "[shader][apply_gamma_pwl][fxaa_luma]") {
  VulkanTestDevice device;
  if (!device.Initialize()) {
    WARN("Vulkan not available, skipping test");
    return;
  }

  // Load native SPIR-V with FXAA luma
  std::vector<uint32_t> spirv(
      apply_gamma_pwl_fxaa_luma_cs_spirv,
      apply_gamma_pwl_fxaa_luma_cs_spirv +
          sizeof(apply_gamma_pwl_fxaa_luma_cs_spirv) / sizeof(uint32_t));

  REQUIRE(SPIRVCrossWrapper::ValidateSPIRV(spirv));

  ComputeTestHarness harness(&device, spirv);
  REQUIRE(harness.IsValid());

  const uint32_t width = 4;
  const uint32_t height = 2;

  struct PushConstants {
    uint32_t width;
    uint32_t height;
  } push_const = {width, height};
  harness.SetPushConstants(push_const);

  // Create linear gamma ramp
  std::vector<uint32_t> gamma_ramp(128 * 3 * 2);
  for (uint32_t i = 0; i < 128; ++i) {
    for (uint32_t c = 0; c < 3; ++c) {
      uint32_t entry_idx = (i * 3 + c) * 2;
      gamma_ramp[entry_idx] = (i * 8) << 6;
      gamma_ramp[entry_idx + 1] = 8 << 6;
    }
  }
  harness.SetTexelBuffer(0, gamma_ramp, vk::Format::eR32G32Uint, 0);

  // Create source texture with different RGB values to test luma calculation
  std::vector<float> source_tex(width * height * 4);
  uint32_t test_values[][3] = {
      {512, 512, 512},  // Gray - luma = 0.5
      {1023, 0, 0},     // Red - luma weighted by 0.299
      {0, 1023, 0},     // Green - luma weighted by 0.587
      {0, 0, 1023},     // Blue - luma weighted by 0.114
      {512, 256, 128},  // Mixed RGB
      {768, 512, 256},  // Another mix
      {256, 768, 512},  // Another mix
      {128, 384, 896},  // Another mix
  };

  for (uint32_t i = 0; i < width * height; ++i) {
    uint32_t idx = i * 4;
    source_tex[idx + 0] = float(test_values[i][0]) / 1023.0f;  // R
    source_tex[idx + 1] = float(test_values[i][1]) / 1023.0f;  // G
    source_tex[idx + 2] = float(test_values[i][2]) / 1023.0f;  // B
    source_tex[idx + 3] = 1.0f;                                // A
  }

  harness.SetTexture2D(0, width, height, vk::Format::eR32G32B32A32Sfloat,
                       source_tex, 1);

  harness.AllocateOutputImage2D(0, width, height,
                                vk::Format::eR16G16B16A16Sfloat, 2);

  harness.Dispatch(1, 1, 1);

  auto output = harness.ReadOutputImage2D<uint16_t>(0, 2);
  REQUIRE(output.size() == width * height * 4);

  // Validate RGB and alpha (luma) output
  for (uint32_t i = 0; i < width * height; ++i) {
    float expected_rgb[3];
    for (uint32_t c = 0; c < 3; ++c) {
      uint32_t input_val = test_values[i][c];
      uint32_t ramp_index = input_val >> 3;
      uint32_t base = gamma_ramp[(ramp_index * 3 + c) * 2];
      uint32_t delta = gamma_ramp[(ramp_index * 3 + c) * 2 + 1];
      expected_rgb[c] = CalculateExpectedPWLGamma(input_val, base, delta);

      uint16_t output_half = output[i * 4 + c];
      float actual = Half2Float(output_half);
      REQUIRE(actual == Approx(expected_rgb[c]).margin(0.001f));
    }

    // Verify alpha contains perceptual luma: dot(RGB, [0.299, 0.587, 0.114])
    float expected_luma = expected_rgb[0] * 0.299f + expected_rgb[1] * 0.587f +
                          expected_rgb[2] * 0.114f;
    uint16_t alpha_half = output[i * 4 + 3];
    float actual_luma = Half2Float(alpha_half);
    REQUIRE(actual_luma == Approx(expected_luma).margin(0.001f));
  }
}

TEST_CASE("apply_gamma_pwl: Edge cases and saturation",
          "[shader][apply_gamma_pwl][edge_cases]") {
  VulkanTestDevice device;
  if (!device.Initialize()) {
    WARN("Vulkan not available, skipping test");
    return;
  }

  std::vector<uint32_t> spirv(
      apply_gamma_pwl_cs_spirv,
      apply_gamma_pwl_cs_spirv +
          sizeof(apply_gamma_pwl_cs_spirv) / sizeof(uint32_t));

  REQUIRE(SPIRVCrossWrapper::ValidateSPIRV(spirv));

  ComputeTestHarness harness(&device, spirv);
  REQUIRE(harness.IsValid());

  const uint32_t width = 8;
  const uint32_t height = 1;

  struct PushConstants {
    uint32_t width;
    uint32_t height;
  } push_const = {width, height};
  harness.SetPushConstants(push_const);

  // Create gamma ramp with edge cases
  std::vector<uint32_t> gamma_ramp(128 * 3 * 2);
  for (uint32_t i = 0; i < 128; ++i) {
    for (uint32_t c = 0; c < 3; ++c) {
      uint32_t entry_idx = (i * 3 + c) * 2;
      if (i == 0) {
        // Entry 0: all zeros
        gamma_ramp[entry_idx] = 0;
        gamma_ramp[entry_idx + 1] = 0;
      } else if (i == 127) {
        // Entry 127: maximum values that saturate to 1.0
        gamma_ramp[entry_idx] = 0xFFFFFFFF;
        gamma_ramp[entry_idx + 1] = 0xFFFFFFFF;
      } else {
        // Linear mapping for middle entries
        gamma_ramp[entry_idx] = (i * 8) << 6;
        gamma_ramp[entry_idx + 1] = 8 << 6;
      }
    }
  }
  harness.SetTexelBuffer(0, gamma_ramp, vk::Format::eR32G32Uint, 0);

  // Test edge case input values
  std::vector<float> source_tex(width * height * 4);
  uint32_t test_values[] = {
      0,     // Minimum value (maps to entry 0)
      1,     // Minimum + 1
      7,     // Maximum multiplier (input & 7 = 7)
      1016,  // Near maximum
      1020,  // Very close to maximum
      1023,  // Maximum value (maps to entry 127)
      512,   // Middle value
      8,     // Boundary (maps to entry 1)
  };

  for (uint32_t i = 0; i < width; ++i) {
    uint32_t idx = i * 4;
    source_tex[idx + 0] = float(test_values[i]) / 1023.0f;
    source_tex[idx + 1] = float(test_values[i]) / 1023.0f;
    source_tex[idx + 2] = float(test_values[i]) / 1023.0f;
    source_tex[idx + 3] = 1.0f;
  }

  harness.SetTexture2D(0, width, height, vk::Format::eR32G32B32A32Sfloat,
                       source_tex, 1);

  harness.AllocateOutputImage2D(0, width, height,
                                vk::Format::eR16G16B16A16Sfloat, 2);

  harness.Dispatch(1, 1, 1);

  auto output = harness.ReadOutputImage2D<uint16_t>(0, 2);
  REQUIRE(output.size() == width * height * 4);

  // Validate edge cases
  for (uint32_t i = 0; i < width; ++i) {
    uint32_t input_val = test_values[i];
    uint32_t ramp_index = input_val >> 3;

    for (uint32_t c = 0; c < 3; ++c) {
      uint32_t base = gamma_ramp[(ramp_index * 3 + c) * 2];
      uint32_t delta = gamma_ramp[(ramp_index * 3 + c) * 2 + 1];
      float expected = CalculateExpectedPWLGamma(input_val, base, delta);

      uint16_t output_half = output[i * 4 + c];
      float actual = Half2Float(output_half);

      REQUIRE(actual == Approx(expected).margin(0.001f));
      // Verify saturation - output should never exceed 1.0
      REQUIRE(actual <= 1.0f);
    }
  }
}

TEST_CASE("apply_gamma_pwl: All multiplier values",
          "[shader][apply_gamma_pwl][multipliers]") {
  VulkanTestDevice device;
  if (!device.Initialize()) {
    WARN("Vulkan not available, skipping test");
    return;
  }

  std::vector<uint32_t> spirv(
      apply_gamma_pwl_cs_spirv,
      apply_gamma_pwl_cs_spirv +
          sizeof(apply_gamma_pwl_cs_spirv) / sizeof(uint32_t));

  REQUIRE(SPIRVCrossWrapper::ValidateSPIRV(spirv));

  ComputeTestHarness harness(&device, spirv);
  REQUIRE(harness.IsValid());

  const uint32_t width = 8;
  const uint32_t height = 1;

  struct PushConstants {
    uint32_t width;
    uint32_t height;
  } push_const = {width, height};
  harness.SetPushConstants(push_const);

  // Create linear gamma ramp
  std::vector<uint32_t> gamma_ramp(128 * 3 * 2);
  for (uint32_t i = 0; i < 128; ++i) {
    for (uint32_t c = 0; c < 3; ++c) {
      uint32_t entry_idx = (i * 3 + c) * 2;
      gamma_ramp[entry_idx] = (i * 8) << 6;
      gamma_ramp[entry_idx + 1] = 8 << 6;
    }
  }
  harness.SetTexelBuffer(0, gamma_ramp, vk::Format::eR32G32Uint, 0);

  // Test all 8 multiplier values (input & 7)
  // Using base value 64 (ramp index 8) to test multipliers 0-7
  std::vector<float> source_tex(width * height * 4);
  for (uint32_t mult = 0; mult < 8; ++mult) {
    uint32_t input_val = (8 << 3) | mult;  // ramp_index=8, multiplier=mult
    uint32_t idx = mult * 4;
    source_tex[idx + 0] = float(input_val) / 1023.0f;
    source_tex[idx + 1] = float(input_val) / 1023.0f;
    source_tex[idx + 2] = float(input_val) / 1023.0f;
    source_tex[idx + 3] = 1.0f;
  }

  harness.SetTexture2D(0, width, height, vk::Format::eR32G32B32A32Sfloat,
                       source_tex, 1);

  harness.AllocateOutputImage2D(0, width, height,
                                vk::Format::eR16G16B16A16Sfloat, 2);

  harness.Dispatch(1, 1, 1);

  auto output = harness.ReadOutputImage2D<uint16_t>(0, 2);
  REQUIRE(output.size() == width * height * 4);

  // Validate all 8 multiplier values produce correct linear interpolation
  for (uint32_t mult = 0; mult < 8; ++mult) {
    uint32_t input_val = (8 << 3) | mult;
    uint32_t ramp_index = input_val >> 3;

    for (uint32_t c = 0; c < 3; ++c) {
      uint32_t base = gamma_ramp[(ramp_index * 3 + c) * 2];
      uint32_t delta = gamma_ramp[(ramp_index * 3 + c) * 2 + 1];
      float expected = CalculateExpectedPWLGamma(input_val, base, delta);

      uint16_t output_half = output[mult * 4 + c];
      float actual = Half2Float(output_half);

      REQUIRE(actual == Approx(expected).margin(0.001f));
    }
  }
}

TEST_CASE("apply_gamma_pwl: Non-linear gamma curves",
          "[shader][apply_gamma_pwl][curves]") {
  VulkanTestDevice device;
  if (!device.Initialize()) {
    WARN("Vulkan not available, skipping test");
    return;
  }

  std::vector<uint32_t> spirv(
      apply_gamma_pwl_cs_spirv,
      apply_gamma_pwl_cs_spirv +
          sizeof(apply_gamma_pwl_cs_spirv) / sizeof(uint32_t));

  REQUIRE(SPIRVCrossWrapper::ValidateSPIRV(spirv));

  ComputeTestHarness harness(&device, spirv);
  REQUIRE(harness.IsValid());

  const uint32_t width = 4;
  const uint32_t height = 2;

  struct PushConstants {
    uint32_t width;
    uint32_t height;
  } push_const = {width, height};
  harness.SetPushConstants(push_const);

  // Create non-linear gamma ramp with different curves per channel
  std::vector<uint32_t> gamma_ramp(128 * 3 * 2);
  for (uint32_t i = 0; i < 128; ++i) {
    // R channel: steep curve (high delta)
    gamma_ramp[(i * 3 + 0) * 2] = (i * 4) << 6;  // base
    gamma_ramp[(i * 3 + 0) * 2 + 1] = 16 << 6;   // delta (2x normal)

    // G channel: flat curve (low delta)
    gamma_ramp[(i * 3 + 1) * 2] = (i * 12) << 6;  // base
    gamma_ramp[(i * 3 + 1) * 2 + 1] = 4 << 6;     // delta (0.5x normal)

    // B channel: inverted curve (decreasing)
    gamma_ramp[(i * 3 + 2) * 2] = ((127 - i) * 8) << 6;  // base (inverted)
    gamma_ramp[(i * 3 + 2) * 2 + 1] = 8 << 6;            // delta
  }
  harness.SetTexelBuffer(0, gamma_ramp, vk::Format::eR32G32Uint, 0);

  // Test with various input values
  std::vector<float> source_tex(width * height * 4);
  uint32_t test_values[] = {128, 256, 512, 768, 192, 384, 640, 896};

  for (uint32_t i = 0; i < width * height; ++i) {
    uint32_t idx = i * 4;
    source_tex[idx + 0] = float(test_values[i]) / 1023.0f;
    source_tex[idx + 1] = float(test_values[i]) / 1023.0f;
    source_tex[idx + 2] = float(test_values[i]) / 1023.0f;
    source_tex[idx + 3] = 1.0f;
  }

  harness.SetTexture2D(0, width, height, vk::Format::eR32G32B32A32Sfloat,
                       source_tex, 1);

  harness.AllocateOutputImage2D(0, width, height,
                                vk::Format::eR16G16B16A16Sfloat, 2);

  harness.Dispatch(1, 1, 1);

  auto output = harness.ReadOutputImage2D<uint16_t>(0, 2);
  REQUIRE(output.size() == width * height * 4);

  // Validate non-linear curves produce different results per channel
  for (uint32_t i = 0; i < width * height; ++i) {
    uint32_t input_val = test_values[i];
    uint32_t ramp_index = input_val >> 3;

    for (uint32_t c = 0; c < 3; ++c) {
      uint32_t base = gamma_ramp[(ramp_index * 3 + c) * 2];
      uint32_t delta = gamma_ramp[(ramp_index * 3 + c) * 2 + 1];
      float expected = CalculateExpectedPWLGamma(input_val, base, delta);

      uint16_t output_half = output[i * 4 + c];
      float actual = Half2Float(output_half);

      REQUIRE(actual == Approx(expected).margin(0.001f));
    }
  }
}
