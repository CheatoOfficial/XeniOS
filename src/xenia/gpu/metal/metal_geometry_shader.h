/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_GEOMETRY_SHADER_H_
#define XENIA_GPU_METAL_METAL_GEOMETRY_SHADER_H_

#include <vector>

#include "xenia/gpu/dxbc_geometry_shader.h"

namespace xe {
namespace gpu {
namespace metal {

// The geometry-shader key, key derivation, and DXBC generation are shared
// across the D3D12 and Metal backends (xenia/gpu/dxbc_geometry_shader.h);
// re-exported here so existing Metal call sites keep their unqualified names.
using xe::gpu::CreateDxbcGeometryShader;
using xe::gpu::GeometryShaderKey;
using xe::gpu::GetGeometryShaderKey;
using xe::gpu::PipelineGeometryShader;

// Returns the cached DXBC geometry shader for the key. The Metal backend then
// converts the returned DXBC through the Metal Shader Converter.
const std::vector<uint32_t>& GetGeometryShader(GeometryShaderKey key);

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_GEOMETRY_SHADER_H_
