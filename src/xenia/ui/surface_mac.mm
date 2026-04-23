/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/surface_mac.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

namespace xe {
namespace ui {

bool MacNSViewSurface::GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const {
  if (!view_) {
    width_out = 0;
    height_out = 0;
    return false;
  }

  // Logical coords to match Window size reporting; backing coords would be
  // Retina physical pixels.
  NSRect bounds = [view_ bounds];
  width_out = static_cast<uint32_t>(bounds.size.width);
  height_out = static_cast<uint32_t>(bounds.size.height);
  return true;
}

CAMetalLayer* MacNSViewSurface::GetOrCreateMetalLayer() const {
  if (!view_) {
    return nullptr;
  }
  CALayer* layer = [view_ layer];
  if ([layer isKindOfClass:[CAMetalLayer class]]) {
    return static_cast<CAMetalLayer*>(layer);
  }
  CAMetalLayer* metalLayer = [CAMetalLayer layer];
  [view_ setWantsLayer:YES];
  [view_ setLayer:metalLayer];
  metalLayer.framebufferOnly = YES;
  metalLayer.opaque = YES;
  return metalLayer;
}

}  // namespace ui
}  // namespace xe
