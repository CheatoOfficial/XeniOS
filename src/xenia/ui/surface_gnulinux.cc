/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/surface_gnulinux.h"

#include <cstdlib>

namespace xe {
namespace ui {

bool XcbWindowSurface::GetSizeImpl(uint32_t& width_out,
                                   uint32_t& height_out) const {
  xcb_get_geometry_reply_t* reply = xcb_get_geometry_reply(
      connection_, xcb_get_geometry(connection_, window_), nullptr);
  if (!reply) {
    return false;
  }
  width_out = reply->width;
  height_out = reply->height;
  std::free(reply);
  return true;
}

bool WaylandWindowSurface::GetSizeImpl(uint32_t& width_out,
                                       uint32_t& height_out) const {
  // Return the size provided during construction/updates
  width_out = width_;
  height_out = height_;
  return width_ > 0 && height_ > 0;
}

}  // namespace ui
}  // namespace xe
