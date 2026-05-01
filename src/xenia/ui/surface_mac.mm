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
  CALayer* mainLayer = [view_ layer];
  if ([mainLayer isKindOfClass:[CAMetalLayer class]]) {
    return static_cast<CAMetalLayer*>(mainLayer);
  }
  if (mainLayer) {
    for (CALayer* sub in mainLayer.sublayers) {
      if ([sub isKindOfClass:[CAMetalLayer class]]) {
        return static_cast<CAMetalLayer*>(sub);
      }
    }
  }
  // Attach as a sublayer rather than replacing the view's main layer: wx has
  // already set wantsLayer:YES during construction, which precludes the
  // setLayer-before-wantsLayer order required for a layer-hosting view.
  if (!mainLayer) {
    [view_ setWantsLayer:YES];
    mainLayer = [view_ layer];
  }
  CAMetalLayer* metalLayer = [CAMetalLayer layer];
  metalLayer.framebufferOnly = YES;
  metalLayer.opaque = YES;
  metalLayer.frame = mainLayer.bounds;
  metalLayer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
  [mainLayer addSublayer:metalLayer];
  return metalLayer;
}

}  // namespace ui
}  // namespace xe
