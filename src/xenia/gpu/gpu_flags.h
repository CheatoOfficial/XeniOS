/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GPU_FLAGS_H_
#define XENIA_GPU_GPU_FLAGS_H_
#include "xenia/base/cvar.h"

DECLARE_path(trace_gpu_prefix);
DECLARE_bool(trace_gpu_stream);

DECLARE_path(dump_shaders);

DECLARE_bool(vsync);

DECLARE_uint64(framerate_limit);

void SetVsync(bool value);
void SetFramerateLimit(uint64_t value);

DECLARE_bool(gpu_allow_invalid_fetch_constants);

DECLARE_bool(non_seamless_cube_map);

DECLARE_bool(half_pixel_offset);

DECLARE_int32(query_occlusion_sample_lower_threshold);

DECLARE_int32(query_occlusion_sample_upper_threshold);

DECLARE_bool(occlusion_query_enable);

void SetOcclusionQueryEnable(bool value);

DECLARE_bool(disassemble_pm4);

DECLARE_bool(gpu_debug_markers);

// Returns true if GPU debug markers should be enabled.
// Checks the CVAR and also detects if RenderDoc is attached.
// Result is cached after first call for efficiency.
bool IsGpuDebugMarkersEnabled();

DECLARE_string(render_target_path);

#define XE_GPU_FINE_GRAINED_DRAW_SCOPES 1

#endif  // XENIA_GPU_GPU_FLAGS_H_
