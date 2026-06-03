/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/touch/touch_hid_ios.h"

#include "xenia/hid/touch/touch_input_driver_ios.h"

namespace xe {
namespace hid {
namespace touch {

std::unique_ptr<InputDriver> Create(xe::ui::Window* window,
                                    size_t window_z_order) {
  return std::make_unique<TouchInputDriver>(window, window_z_order);
}

}  // namespace touch
}  // namespace hid
}  // namespace xe
