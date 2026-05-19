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

  NSRect bounds = [view_ bounds];
  // Use logical coordinates to match Window size reporting
  // (backing coordinates would be for Retina physical pixels)

  width_out = static_cast<uint32_t>(bounds.size.width);
  height_out = static_cast<uint32_t>(bounds.size.height);

  return true;
}

double MacNSViewSurface::GetBackingScale() const {
  if (!view_) {
    return 1.0;
  }

  NSScreen* screen = [view_ window].screen;
  if (!screen) {
    screen = NSScreen.mainScreen;
  }
  return screen ? screen.backingScaleFactor : 1.0;
}

void MacNSViewSurface::ConfigureMetalLayer(uint32_t drawable_width, uint32_t drawable_height,
                                           double contents_scale) const {
  CAMetalLayer* metal_layer = GetOrCreateMetalLayer();
  if (!metal_layer) {
    return;
  }

  CALayer* main_layer = [view_ layer];
  metal_layer.frame = main_layer.bounds;
  metal_layer.contentsScale = contents_scale;
  metal_layer.drawableSize =
      CGSizeMake(drawable_width * contents_scale, drawable_height * contents_scale);
}

CAMetalLayer* MacNSViewSurface::GetOrCreateMetalLayer() const {
  if (!view_) {
    return nullptr;
  }

  // Check if the view already has a CAMetalLayer
  CALayer* layer = [view_ layer];
  if ([layer isKindOfClass:[CAMetalLayer class]]) {
    return static_cast<CAMetalLayer*>(layer);
  }

  // Create and set a new CAMetalLayer
  CAMetalLayer* metalLayer = [CAMetalLayer layer];
  [view_ setWantsLayer:YES];
  [view_ setLayer:metalLayer];

  // Configure the metal layer
  metalLayer.framebufferOnly = YES;
  metalLayer.opaque = YES;

  return metalLayer;
}

}  // namespace ui
}  // namespace xe
