/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/cvar.h"

// CPU tests link emulator setup code, but not the platform app main that owns
// these backend selection cvars. Keep the test defaults headless.
DEFINE_string(apu, "nop", "Audio system used by CPU tests.", "APU");
DEFINE_string(gpu, "null", "Graphics system used by CPU tests.", "GPU");
