/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#import "xenia/ui/touch_controls_ios.h"

#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <cmath>

namespace {

static UIColor* XeniaTouchOuterColor(void) {
  return [UIColor colorWithWhite:0.08f alpha:0.82f];
}

static UIColor* XeniaTouchInnerColor(void) {
  return [UIColor colorWithWhite:0.92f alpha:0.84f];
}

static UIColor* XeniaTouchPressedColor(void) {
  return [UIColor colorWithRed:0.42f green:0.87f blue:0.63f alpha:0.92f];
}

static UIColor* XeniaFaceButtonColor(NSInteger tag) {
  switch (tag) {
    case 9:
      return [UIColor colorWithRed:0.23f green:0.48f blue:0.96f alpha:0.92f];
    case 10:
      return [UIColor colorWithRed:0.96f green:0.82f blue:0.20f alpha:0.92f];
    case 11:
      return [UIColor colorWithRed:0.33f green:0.78f blue:0.33f alpha:0.92f];
    case 12:
      return [UIColor colorWithRed:0.91f green:0.28f blue:0.24f alpha:0.92f];
    default:
      return XeniaTouchInnerColor();
  }
}

static UIColor* XeniaDimmedFaceButtonColor(NSInteger tag) {
  switch (tag) {
    case 9:
      return [UIColor colorWithRed:0.17f green:0.36f blue:0.74f alpha:0.92f];
    case 10:
      return [UIColor colorWithRed:0.74f green:0.63f blue:0.15f alpha:0.92f];
    case 11:
      return [UIColor colorWithRed:0.23f green:0.56f blue:0.23f alpha:0.92f];
    case 12:
      return [UIColor colorWithRed:0.67f green:0.20f blue:0.17f alpha:0.92f];
    default:
      return XeniaTouchPressedColor();
  }
}

static UIColor* XeniaTouchCenterColor(void) {
  return [UIColor colorWithWhite:0.62f alpha:0.84f];
}

static UIColor* XeniaTouchMenuColor(void) {
  return [UIColor colorWithWhite:0.92f alpha:0.90f];
}

static NSDictionary* XeniaDisabledLayerActions(void) {
  static NSDictionary* actions = nil;
  static dispatch_once_t once_token;
  dispatch_once(&once_token, ^{
    actions = [[NSDictionary alloc] initWithObjectsAndKeys:
        [NSNull null], @"bounds",
        [NSNull null], @"contents",
        [NSNull null], @"frame",
        [NSNull null], @"hidden",
        [NSNull null], @"opacity",
        [NSNull null], @"position",
        [NSNull null], @"transform",
        nil];
  });
  return actions;
}

static void XeniaPrepareLayer(CALayer* layer) {
  layer.actions = XeniaDisabledLayerActions();
  layer.contentsGravity = kCAGravityResize;
  layer.contentsScale = UIScreen.mainScreen.scale;
  layer.drawsAsynchronously = YES;
  layer.shouldRasterize = YES;
  layer.rasterizationScale = UIScreen.mainScreen.scale;
  layer.opaque = NO;
}

static int16_t XeniaThumbAxis(CGFloat value) {
  value = std::max<CGFloat>(-1.0f, std::min<CGFloat>(1.0f, value));
  if (value >= 1.0f) {
    return INT16_MAX;
  }
  if (value <= -1.0f) {
    return INT16_MIN;
  }
  return static_cast<int16_t>(std::lrint(value * 32767.0f));
}

class ThumbVector {
 public:
  ThumbVector() = default;
  ThumbVector(CGFloat x, CGFloat y) : x(x), y(y) {}
  CGFloat x = 0.0f;
  CGFloat y = 0.0f;
};

static CGFloat XeniaThumbRadius(CGRect frame) {
  return MIN(CGRectGetWidth(frame), CGRectGetHeight(frame)) * 0.28f;
}

static CGFloat XeniaThumbKnobSize(CGRect frame) {
  return MIN(CGRectGetWidth(frame), CGRectGetHeight(frame)) *
         (UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad ? 0.52f : 0.54f);
}

static CGPoint XeniaThumbCenter(CGRect frame) {
  return CGPointMake(CGRectGetMidX(frame), CGRectGetMidY(frame));
}

static CGRect XeniaThumbKnobRect(CGRect frame, const ThumbVector& vector) {
  CGFloat knob_size = XeniaThumbKnobSize(frame);
  CGFloat radius = XeniaThumbRadius(frame);
  CGPoint center = XeniaThumbCenter(frame);
  CGPoint knob_center = CGPointMake(center.x + vector.x * radius, center.y - vector.y * radius);
  return CGRectMake(knob_center.x - knob_size * 0.5f,
                    knob_center.y - knob_size * 0.5f,
                    knob_size,
                    knob_size);
}

static UIFont* XeniaFont(CGFloat size, UIFontWeight weight) {
  return [UIFont systemFontOfSize:size weight:weight];
}

static NSString* XeniaButtonTitle(NSInteger tag) {
  switch (tag) {
    case 1: return @"LT";
    case 2: return @"LB";
    case 3: return @"RB";
    case 4: return @"RT";
    case 5: return @"▲";
    case 6: return @"▼";
    case 7: return @"◀";
    case 8: return @"▶";
    case 9: return @"X";
    case 10: return @"Y";
    case 11: return @"A";
    case 12: return @"B";
    case 13: return @"◀";
    case 14: return @"▶";
    default: return @"";
  }
}

static UIFont* XeniaButtonFontForTag(NSInteger tag) {
  switch (tag) {
    case 1:
    case 2:
    case 3:
    case 4:
      return XeniaFont(24.0f, UIFontWeightSemibold);
    case 13:
    case 14:
      return XeniaFont(24.0f, UIFontWeightBold);
    default:
      return XeniaFont(30.0f, UIFontWeightSemibold);
  }
}

static UIColor* XeniaButtonFillColor(NSInteger tag, BOOL pressed) {
  BOOL is_face_button = tag >= 9 && tag <= 12;
  UIColor* default_color = XeniaFaceButtonColor(tag);
  return is_face_button ? (pressed ? XeniaDimmedFaceButtonColor(tag) : default_color)
                        : (pressed ? XeniaTouchPressedColor() : default_color);
}

static void XeniaDrawTextInRect(NSString* text, CGRect rect, UIFont* font, UIColor* color) {
  if (!text.length) {
    return;
  }
  NSMutableParagraphStyle* style = [[[NSMutableParagraphStyle alloc] init] autorelease];
  style.alignment = NSTextAlignmentCenter;
  NSDictionary* attributes = @{
    NSFontAttributeName : font,
    NSForegroundColorAttributeName : color,
    NSParagraphStyleAttributeName : style,
  };
  CGSize text_size = [text sizeWithAttributes:attributes];
  CGRect text_rect = CGRectMake(CGRectGetMinX(rect),
                                CGRectGetMidY(rect) - text_size.height * 0.5f,
                                CGRectGetWidth(rect),
                                text_size.height);
  [text drawInRect:CGRectIntegral(text_rect) withAttributes:attributes];
}

static void XeniaDrawCircleButton(CGRect rect, UIColor* fill, NSString* title, UIFont* font) {
  CGContextRef ctx = UIGraphicsGetCurrentContext();
  CGContextSetFillColorWithColor(ctx, fill.CGColor);
  CGContextFillEllipseInRect(ctx, rect);
  CGContextSetStrokeColorWithColor(ctx, XeniaTouchOuterColor().CGColor);
  CGContextSetLineWidth(ctx, 7.0f);
  CGContextStrokeEllipseInRect(ctx, CGRectInset(rect, 3.5f, 3.5f));
  XeniaDrawTextInRect(title, rect, font, [UIColor colorWithWhite:0.17f alpha:1.0f]);
}

static void XeniaDrawRoundedButton(CGRect rect, UIColor* fill, NSString* title, UIFont* font,
                                   CGFloat radius) {
  UIBezierPath* path = [UIBezierPath bezierPathWithRoundedRect:rect cornerRadius:radius];
  [fill setFill];
  [path fill];
  path.lineWidth = 7.0f;
  [XeniaTouchOuterColor() setStroke];
  [path stroke];
  XeniaDrawTextInRect(title, rect, font, [UIColor colorWithWhite:0.17f alpha:1.0f]);
}

static void XeniaDrawMenuButton(CGRect rect, BOOL pressed) {
  UIColor* fill = pressed ? XeniaTouchPressedColor() : XeniaTouchMenuColor();
  CGContextRef ctx = UIGraphicsGetCurrentContext();
  CGContextSetFillColorWithColor(ctx, fill.CGColor);
  CGContextFillEllipseInRect(ctx, rect);
  CGContextSetStrokeColorWithColor(ctx, XeniaTouchOuterColor().CGColor);
  CGContextSetLineWidth(ctx, 6.0f);
  CGContextStrokeEllipseInRect(ctx, CGRectInset(rect, 3.0f, 3.0f));
  XeniaDrawTextInRect(@"⎋", rect, XeniaFont(26.0f, UIFontWeightBold),
                      [UIColor colorWithWhite:0.14f alpha:1.0f]);
}

static void XeniaDrawThumbKnob(CGRect rect) {
  CGContextRef ctx = UIGraphicsGetCurrentContext();
  CGContextSetFillColorWithColor(ctx, XeniaTouchCenterColor().CGColor);
  CGContextFillEllipseInRect(ctx, rect);
  CGContextSetStrokeColorWithColor(ctx, XeniaTouchOuterColor().CGColor);
  CGContextSetLineWidth(ctx, 6.0f);
  CGContextStrokeEllipseInRect(ctx, CGRectInset(rect, 3.0f, 3.0f));
}

static UIImage* XeniaRenderImage(CGSize size, void (^draw_block)(CGRect rect)) {
  if (size.width <= 0.0f || size.height <= 0.0f) {
    return nil;
  }
  UIGraphicsBeginImageContextWithOptions(size, NO, 0.0f);
  CGRect rect = CGRectMake(0.0f, 0.0f, size.width, size.height);
  draw_block(rect);
  UIImage* image = UIGraphicsGetImageFromCurrentImageContext();
  UIGraphicsEndImageContext();
  return image;
}

static UIImage* XeniaCreateButtonImage(NSInteger tag, CGSize size, BOOL pressed) {
  UIColor* fill = XeniaButtonFillColor(tag, pressed);
  NSString* title = XeniaButtonTitle(tag);
  UIFont* font = XeniaButtonFontForTag(tag);
  if (tag <= 4) {
    return XeniaRenderImage(size, ^(CGRect rect) {
      XeniaDrawRoundedButton(rect, fill, title, font, roundf(CGRectGetHeight(rect) * 0.32f));
    });
  }
  return XeniaRenderImage(size, ^(CGRect rect) {
    XeniaDrawCircleButton(rect, fill, title, font);
  });
}

static UIImage* XeniaCreateMenuImage(CGSize size, BOOL pressed) {
  return XeniaRenderImage(size, ^(CGRect rect) {
    XeniaDrawMenuButton(rect, pressed);
  });
}

static UIImage* XeniaCreateThumbImage(CGSize size) {
  return XeniaRenderImage(size, ^(CGRect rect) {
    XeniaDrawThumbKnob(rect);
  });
}

}  // namespace

@interface XeniaTouchControlsView () {
 @private
  xe::hid::X_INPUT_STATE state_;
  CGRect button_frames_[15];
  CGRect menu_frame_;
  CGRect thumb_frames_[2];
  ThumbVector thumb_vectors_[2];
  BOOL button_pressed_[15];
  BOOL menu_pressed_;
  UITouch* button_touches_[15];
  UITouch* thumb_touches_[2];
  CGPoint thumb_touch_start_points_[2];
  BOOL thumb_touch_dragged_[2];
  UITouch* menu_touch_;
  CGSize button_image_sizes_[15];
  UIImage* button_images_[15][2];
  CGSize menu_image_size_;
  UIImage* menu_images_[2];
  CGSize knob_image_size_;
  UIImage* knob_image_;
  CALayer* button_layers_[15];
  CALayer* menu_layer_;
  CALayer* thumb_layers_[2];
  void (^stateDidChangeHandler_)(void);
  void (^menuButtonTappedHandler_)(void);
}
- (void)rebuildAssetsIfNeededForButtonTag:(NSInteger)tag size:(CGSize)size;
- (void)rebuildMenuAssetsIfNeeded:(CGSize)size;
- (void)rebuildKnobAssetIfNeeded:(CGSize)size;
- (void)updateButtonLayerForTag:(NSInteger)tag;
- (void)updateMenuLayer;
- (CGRect)thumbKnobRectForIndex:(NSInteger)index;
- (CGRect)thumbHitRectForIndex:(NSInteger)index;
- (void)updateThumbLayerForIndex:(NSInteger)index;
- (void)setButtonTag:(NSInteger)tag pressed:(BOOL)pressed emit:(BOOL)emit;
- (void)setMenuPressed:(BOOL)pressed;
- (void)updateThumbIndex:(NSInteger)index forPoint:(CGPoint)point emit:(BOOL)emit;
- (void)resetThumbIndex:(NSInteger)index emit:(BOOL)emit;
- (void)emitThumbClickForIndex:(NSInteger)index;
- (NSInteger)buttonTagForTouchPoint:(CGPoint)point;
- (NSInteger)thumbIndexForTouchPoint:(CGPoint)point;
- (BOOL)isPointInsideMenu:(CGPoint)point;
- (void)emitStateChanged;
- (void)setDigitalButton:(uint16_t)mask pressed:(BOOL)pressed;
- (BOOL)isButtonPressed:(NSInteger)tag;
@end

@implementation XeniaTouchControlsView
@synthesize stateDidChangeHandler = stateDidChangeHandler_;
@synthesize menuButtonTappedHandler = menuButtonTappedHandler_;

- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) {
    self.backgroundColor = [UIColor clearColor];
    self.opaque = NO;
    self.multipleTouchEnabled = YES;
    self.exclusiveTouch = NO;
    self.layer.drawsAsynchronously = YES;
    self.layer.shouldRasterize = NO;

    memset(button_frames_, 0, sizeof(button_frames_));
    memset(&menu_frame_, 0, sizeof(menu_frame_));
    memset(thumb_frames_, 0, sizeof(thumb_frames_));
    memset(button_pressed_, 0, sizeof(button_pressed_));
    memset(button_touches_, 0, sizeof(button_touches_));
    memset(thumb_touches_, 0, sizeof(thumb_touches_));
    memset(thumb_touch_start_points_, 0, sizeof(thumb_touch_start_points_));
    memset(thumb_touch_dragged_, 0, sizeof(thumb_touch_dragged_));
    memset(button_image_sizes_, 0, sizeof(button_image_sizes_));
    memset(button_images_, 0, sizeof(button_images_));
    memset(menu_images_, 0, sizeof(menu_images_));
    memset(thumb_vectors_, 0, sizeof(thumb_vectors_));
    menu_touch_ = nil;
    menu_pressed_ = NO;
    menu_image_size_ = CGSizeZero;
    knob_image_size_ = CGSizeZero;
    knob_image_ = nil;

    for (NSInteger tag = 1; tag <= 14; ++tag) {
      button_layers_[tag] = [[CALayer alloc] init];
      XeniaPrepareLayer(button_layers_[tag]);
      [self.layer addSublayer:button_layers_[tag]];
    }
    menu_layer_ = [[CALayer alloc] init];
    XeniaPrepareLayer(menu_layer_);
    [self.layer addSublayer:menu_layer_];
    for (NSInteger index = 0; index < 2; ++index) {
      thumb_layers_[index] = [[CALayer alloc] init];
      XeniaPrepareLayer(thumb_layers_[index]);
      [self.layer addSublayer:thumb_layers_[index]];
    }
  }
  return self;
}

- (void)dealloc {
  [stateDidChangeHandler_ release];
  [menuButtonTappedHandler_ release];
  for (NSInteger tag = 1; tag <= 14; ++tag) {
    [button_images_[tag][0] release];
    [button_images_[tag][1] release];
    [button_layers_[tag] release];
  }
  [menu_images_[0] release];
  [menu_images_[1] release];
  [knob_image_ release];
  [menu_layer_ release];
  [thumb_layers_[0] release];
  [thumb_layers_[1] release];
  [super dealloc];
}

- (BOOL)pointInside:(CGPoint)point withEvent:(UIEvent*)event {
  (void)event;
  if ([self thumbIndexForTouchPoint:point] >= 0) {
    return YES;
  }
  if ([self isPointInsideMenu:point]) {
    return YES;
  }
  return [self buttonTagForTouchPoint:point] >= 0;
}

- (void)rebuildAssetsIfNeededForButtonTag:(NSInteger)tag size:(CGSize)size {
  if (CGSizeEqualToSize(size, button_image_sizes_[tag])) {
    return;
  }
  [button_images_[tag][0] release];
  [button_images_[tag][1] release];
  button_images_[tag][0] = [XeniaCreateButtonImage(tag, size, NO) retain];
  button_images_[tag][1] = [XeniaCreateButtonImage(tag, size, YES) retain];
  button_image_sizes_[tag] = size;
}

- (void)rebuildMenuAssetsIfNeeded:(CGSize)size {
  if (CGSizeEqualToSize(size, menu_image_size_)) {
    return;
  }
  [menu_images_[0] release];
  [menu_images_[1] release];
  menu_images_[0] = [XeniaCreateMenuImage(size, NO) retain];
  menu_images_[1] = [XeniaCreateMenuImage(size, YES) retain];
  menu_image_size_ = size;
}

- (void)rebuildKnobAssetIfNeeded:(CGSize)size {
  if (CGSizeEqualToSize(size, knob_image_size_)) {
    return;
  }
  [knob_image_ release];
  knob_image_ = [XeniaCreateThumbImage(size) retain];
  knob_image_size_ = size;
}

- (void)updateButtonLayerForTag:(NSInteger)tag {
  CALayer* layer = button_layers_[tag];
  UIImage* image = button_images_[tag][[self isButtonPressed:tag] ? 1 : 0];
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  layer.frame = button_frames_[tag];
  layer.hidden = CGRectIsEmpty(button_frames_[tag]) || image == nil || self.hidden;
  layer.contents = (id)image.CGImage;
  [CATransaction commit];
}

- (void)updateMenuLayer {
  UIImage* image = menu_images_[menu_pressed_ ? 1 : 0];
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  menu_layer_.frame = menu_frame_;
  menu_layer_.hidden = CGRectIsEmpty(menu_frame_) || image == nil || self.hidden;
  menu_layer_.contents = (id)image.CGImage;
  [CATransaction commit];
}

- (CGRect)thumbKnobRectForIndex:(NSInteger)index {
  return XeniaThumbKnobRect(thumb_frames_[index], thumb_vectors_[index]);
}

- (CGRect)thumbHitRectForIndex:(NSInteger)index {
  return CGRectInset([self thumbKnobRectForIndex:index], -12.0f, -12.0f);
}

- (void)updateThumbLayerForIndex:(NSInteger)index {
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  thumb_layers_[index].frame = [self thumbKnobRectForIndex:index];
  thumb_layers_[index].hidden = self.hidden || knob_image_ == nil;
  thumb_layers_[index].contents = (id)knob_image_.CGImage;
  [CATransaction commit];
}

- (void)setHidden:(BOOL)hidden {
  BOOL changed = self.hidden != hidden;
  [super setHidden:hidden];
  if (!changed) {
    return;
  }
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  for (NSInteger tag = 1; tag <= 14; ++tag) {
    button_layers_[tag].hidden = hidden;
  }
  menu_layer_.hidden = hidden;
  thumb_layers_[0].hidden = hidden;
  thumb_layers_[1].hidden = hidden;
  [CATransaction commit];
}

- (void)layoutSubviews {
  [super layoutSubviews];

  UIEdgeInsets insets = self.safeAreaInsets;
  CGFloat width = CGRectGetWidth(self.bounds);
  CGFloat height = CGRectGetHeight(self.bounds);
  CGFloat short_side = MIN(width, height);
  BOOL is_pad = UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad;

  CGFloat main_button_size = short_side * (is_pad ? 0.098f : 0.136f);
  main_button_size = MAX(54.0f, MIN(main_button_size, 88.0f));
  CGFloat small_button_size = roundf(main_button_size * 0.72f);
  CGFloat cluster_gap = roundf(main_button_size * (is_pad ? 0.44f : 0.40f));
  CGFloat stick_touch_size = roundf(main_button_size * (is_pad ? 1.40f : 1.24f));

  CGFloat horizontal_edge_padding = short_side * (is_pad ? 0.055f : 0.040f);
  CGFloat bottom_padding = 0.0f;
  CGFloat shoulder_width = roundf(main_button_size * 1.14f);
  CGFloat shoulder_height = roundf(main_button_size * 0.82f);
  CGFloat trigger_height = roundf(main_button_size * 1.50f);
  CGFloat shoulder_gap = roundf(main_button_size * 0.22f);
  CGFloat top_margin = is_pad ? (insets.top + short_side * 0.092f) : insets.top;
  CGFloat side_margin = insets.left + short_side * (is_pad ? 0.060f : 0.050f);

  button_frames_[1] = CGRectMake(side_margin, top_margin, shoulder_width, trigger_height);
  button_frames_[2] = CGRectMake(CGRectGetMaxX(button_frames_[1]) + shoulder_gap,
                                 top_margin, shoulder_width, shoulder_height);
  button_frames_[4] = CGRectMake(width - insets.right - (side_margin - insets.left) - shoulder_width,
                                 top_margin, shoulder_width, trigger_height);
  button_frames_[3] = CGRectMake(CGRectGetMinX(button_frames_[4]) - shoulder_gap - shoulder_width,
                                 top_margin, shoulder_width, shoulder_height);

  CGFloat left_cluster_center_x = insets.left + horizontal_edge_padding + main_button_size + cluster_gap;
  CGFloat right_cluster_center_x = width - insets.right - horizontal_edge_padding - main_button_size - cluster_gap;
  CGFloat cluster_center_y =
      height - insets.bottom - bottom_padding - main_button_size - cluster_gap - (is_pad ? 52.0f : 0.0f);

  thumb_frames_[0] = CGRectMake(left_cluster_center_x - stick_touch_size * 0.5f,
                                cluster_center_y - stick_touch_size * 0.5f,
                                stick_touch_size,
                                stick_touch_size);
  thumb_frames_[1] = CGRectMake(right_cluster_center_x - stick_touch_size * 0.5f,
                                cluster_center_y - stick_touch_size * 0.5f,
                                stick_touch_size,
                                stick_touch_size);

  button_frames_[5] = CGRectMake(left_cluster_center_x - main_button_size * 0.5f,
                                 cluster_center_y - main_button_size - cluster_gap,
                                 main_button_size, main_button_size);
  button_frames_[6] = CGRectMake(left_cluster_center_x - main_button_size * 0.5f,
                                 cluster_center_y + cluster_gap,
                                 main_button_size, main_button_size);
  button_frames_[7] = CGRectMake(left_cluster_center_x - main_button_size - cluster_gap,
                                 cluster_center_y - main_button_size * 0.5f,
                                 main_button_size, main_button_size);
  button_frames_[8] = CGRectMake(left_cluster_center_x + cluster_gap,
                                 cluster_center_y - main_button_size * 0.5f,
                                 main_button_size, main_button_size);

  button_frames_[10] = CGRectMake(right_cluster_center_x - main_button_size * 0.5f,
                                  cluster_center_y - main_button_size - cluster_gap,
                                  main_button_size, main_button_size);
  button_frames_[11] = CGRectMake(right_cluster_center_x - main_button_size * 0.5f,
                                  cluster_center_y + cluster_gap,
                                  main_button_size, main_button_size);
  button_frames_[9] = CGRectMake(right_cluster_center_x - main_button_size - cluster_gap,
                                 cluster_center_y - main_button_size * 0.5f,
                                 main_button_size, main_button_size);
  button_frames_[12] = CGRectMake(right_cluster_center_x + cluster_gap,
                                  cluster_center_y - main_button_size * 0.5f,
                                  main_button_size, main_button_size);

  CGFloat menu_size = roundf(main_button_size * 0.78f);
  CGFloat bottom_row_offset = roundf(short_side * 0.018f);
  CGFloat menu_y = height - insets.bottom - bottom_padding - menu_size + bottom_row_offset;
  CGFloat bottom_button_gap = roundf(small_button_size * 0.24f);
  CGFloat menu_center_y = menu_y + menu_size * 0.5f;

  button_frames_[13] = CGRectMake(width * 0.5f - menu_size * 0.5f - bottom_button_gap - small_button_size,
                                  menu_center_y - small_button_size * 0.5f,
                                  small_button_size,
                                  small_button_size);
  button_frames_[14] = CGRectMake(width * 0.5f + menu_size * 0.5f + bottom_button_gap,
                                  menu_center_y - small_button_size * 0.5f,
                                  small_button_size,
                                  small_button_size);
  menu_frame_ = CGRectMake(width * 0.5f - menu_size * 0.5f,
                           menu_y,
                           menu_size,
                           menu_size);

  for (NSInteger tag = 1; tag <= 14; ++tag) {
    [self rebuildAssetsIfNeededForButtonTag:tag size:button_frames_[tag].size];
    [self updateButtonLayerForTag:tag];
  }
  [self rebuildMenuAssetsIfNeeded:menu_frame_.size];
  [self updateMenuLayer];
  [self rebuildKnobAssetIfNeeded:CGSizeMake(XeniaThumbKnobSize(thumb_frames_[0]),
                                            XeniaThumbKnobSize(thumb_frames_[0]))];
  [self updateThumbLayerForIndex:0];
  [self updateThumbLayerForIndex:1];
}

- (xe::hid::X_INPUT_STATE)currentControllerState {
  return state_;
}

- (void)resetControllerState {
  state_ = xe::hid::X_INPUT_STATE{};
  memset(button_pressed_, 0, sizeof(button_pressed_));
  memset(button_touches_, 0, sizeof(button_touches_));
  memset(thumb_touches_, 0, sizeof(thumb_touches_));
  memset(thumb_touch_start_points_, 0, sizeof(thumb_touch_start_points_));
  memset(thumb_touch_dragged_, 0, sizeof(thumb_touch_dragged_));
  thumb_vectors_[0] = ThumbVector();
  thumb_vectors_[1] = ThumbVector();
  menu_pressed_ = NO;
  menu_touch_ = nil;
  for (NSInteger tag = 1; tag <= 14; ++tag) {
    [self updateButtonLayerForTag:tag];
  }
  [self updateMenuLayer];
  [self updateThumbLayerForIndex:0];
  [self updateThumbLayerForIndex:1];
  [self emitStateChanged];
}

- (void)emitStateChanged {
  state_.packet_number += 1;
  if (stateDidChangeHandler_) {
    stateDidChangeHandler_();
  }
}

- (void)setDigitalButton:(uint16_t)mask pressed:(BOOL)pressed {
  uint16_t buttons = static_cast<uint16_t>(state_.gamepad.buttons);
  buttons = pressed ? static_cast<uint16_t>(buttons | mask)
                    : static_cast<uint16_t>(buttons & static_cast<uint16_t>(~mask));
  state_.gamepad.buttons = buttons;
}

- (BOOL)isButtonPressed:(NSInteger)tag {
  switch (tag) {
    case 1:
      return state_.gamepad.left_trigger > 0;
    case 2:
      return (state_.gamepad.buttons & xe::hid::X_INPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    case 3:
      return (state_.gamepad.buttons & xe::hid::X_INPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
    case 4:
      return state_.gamepad.right_trigger > 0;
    case 5:
      return (state_.gamepad.buttons & xe::hid::X_INPUT_GAMEPAD_DPAD_UP) != 0;
    case 6:
      return (state_.gamepad.buttons & xe::hid::X_INPUT_GAMEPAD_DPAD_DOWN) != 0;
    case 7:
      return (state_.gamepad.buttons & xe::hid::X_INPUT_GAMEPAD_DPAD_LEFT) != 0;
    case 8:
      return (state_.gamepad.buttons & xe::hid::X_INPUT_GAMEPAD_DPAD_RIGHT) != 0;
    case 9:
      return (state_.gamepad.buttons & xe::hid::X_INPUT_GAMEPAD_X) != 0;
    case 10:
      return (state_.gamepad.buttons & xe::hid::X_INPUT_GAMEPAD_Y) != 0;
    case 11:
      return (state_.gamepad.buttons & xe::hid::X_INPUT_GAMEPAD_A) != 0;
    case 12:
      return (state_.gamepad.buttons & xe::hid::X_INPUT_GAMEPAD_B) != 0;
    case 13:
      return (state_.gamepad.buttons & xe::hid::X_INPUT_GAMEPAD_BACK) != 0;
    case 14:
      return (state_.gamepad.buttons & xe::hid::X_INPUT_GAMEPAD_START) != 0;
    default:
      return NO;
  }
}

- (void)setButtonTag:(NSInteger)tag pressed:(BOOL)pressed emit:(BOOL)emit {
  if (tag < 1 || tag > 14 || button_pressed_[tag] == pressed) {
    return;
  }
  button_pressed_[tag] = pressed;
  switch (tag) {
    case 1: state_.gamepad.left_trigger = pressed ? 0xFF : 0; break;
    case 2: [self setDigitalButton:xe::hid::X_INPUT_GAMEPAD_LEFT_SHOULDER pressed:pressed]; break;
    case 3: [self setDigitalButton:xe::hid::X_INPUT_GAMEPAD_RIGHT_SHOULDER pressed:pressed]; break;
    case 4: state_.gamepad.right_trigger = pressed ? 0xFF : 0; break;
    case 5: [self setDigitalButton:xe::hid::X_INPUT_GAMEPAD_DPAD_UP pressed:pressed]; break;
    case 6: [self setDigitalButton:xe::hid::X_INPUT_GAMEPAD_DPAD_DOWN pressed:pressed]; break;
    case 7: [self setDigitalButton:xe::hid::X_INPUT_GAMEPAD_DPAD_LEFT pressed:pressed]; break;
    case 8: [self setDigitalButton:xe::hid::X_INPUT_GAMEPAD_DPAD_RIGHT pressed:pressed]; break;
    case 9: [self setDigitalButton:xe::hid::X_INPUT_GAMEPAD_X pressed:pressed]; break;
    case 10: [self setDigitalButton:xe::hid::X_INPUT_GAMEPAD_Y pressed:pressed]; break;
    case 11: [self setDigitalButton:xe::hid::X_INPUT_GAMEPAD_A pressed:pressed]; break;
    case 12: [self setDigitalButton:xe::hid::X_INPUT_GAMEPAD_B pressed:pressed]; break;
    case 13: [self setDigitalButton:xe::hid::X_INPUT_GAMEPAD_BACK pressed:pressed]; break;
    case 14: [self setDigitalButton:xe::hid::X_INPUT_GAMEPAD_START pressed:pressed]; break;
    default: break;
  }
  [self updateButtonLayerForTag:tag];
  if (emit) {
    [self emitStateChanged];
  }
}

- (void)setMenuPressed:(BOOL)pressed {
  if (menu_pressed_ == pressed) {
    return;
  }
  menu_pressed_ = pressed;
  [self updateMenuLayer];
}

- (void)updateThumbIndex:(NSInteger)index forPoint:(CGPoint)point emit:(BOOL)emit {
  CGPoint center = XeniaThumbCenter(thumb_frames_[index]);
  CGFloat dx = point.x - center.x;
  CGFloat dy = center.y - point.y;
  CGFloat radius = XeniaThumbRadius(thumb_frames_[index]);
  if (radius <= 0.0f) {
    return;
  }
  CGFloat length = std::sqrt(dx * dx + dy * dy);
  if (length > radius) {
    dx = dx / length * radius;
    dy = dy / length * radius;
  }
  ThumbVector next_vector(dx / radius, dy / radius);
  if (std::fabs(next_vector.x - thumb_vectors_[index].x) < 0.0005f &&
      std::fabs(next_vector.y - thumb_vectors_[index].y) < 0.0005f) {
    return;
  }
  thumb_vectors_[index] = next_vector;
  if (index == 0) {
    state_.gamepad.thumb_lx = XeniaThumbAxis(next_vector.x);
    state_.gamepad.thumb_ly = XeniaThumbAxis(next_vector.y);
  } else {
    state_.gamepad.thumb_rx = XeniaThumbAxis(next_vector.x);
    state_.gamepad.thumb_ry = XeniaThumbAxis(next_vector.y);
  }
  [self updateThumbLayerForIndex:index];
  if (emit) {
    [self emitStateChanged];
  }
}

- (void)emitThumbClickForIndex:(NSInteger)index {
  const uint16_t mask = index == 0 ? xe::hid::X_INPUT_GAMEPAD_LEFT_THUMB
                                   : xe::hid::X_INPUT_GAMEPAD_RIGHT_THUMB;
  [self setDigitalButton:mask pressed:YES];
  [self emitStateChanged];
  XeniaTouchControlsView* retained_self = [self retain];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 50000000), dispatch_get_main_queue(), ^{
    [retained_self setDigitalButton:mask pressed:NO];
    [retained_self emitStateChanged];
    [retained_self release];
  });
}

- (void)resetThumbIndex:(NSInteger)index emit:(BOOL)emit {
  if (std::fabs(thumb_vectors_[index].x) < 0.0005f &&
      std::fabs(thumb_vectors_[index].y) < 0.0005f) {
    return;
  }
  thumb_vectors_[index] = ThumbVector();
  if (index == 0) {
    state_.gamepad.thumb_lx = 0;
    state_.gamepad.thumb_ly = 0;
  } else {
    state_.gamepad.thumb_rx = 0;
    state_.gamepad.thumb_ry = 0;
  }
  [self updateThumbLayerForIndex:index];
  if (emit) {
    [self emitStateChanged];
  }
}

- (NSInteger)buttonTagForTouchPoint:(CGPoint)point {
  for (NSInteger tag = 1; tag <= 14; ++tag) {
    if (CGRectContainsPoint(button_frames_[tag], point)) {
      return tag;
    }
  }
  return -1;
}

- (NSInteger)thumbIndexForTouchPoint:(CGPoint)point {
  for (NSInteger index = 0; index < 2; ++index) {
    if (CGRectContainsPoint([self thumbHitRectForIndex:index], point)) {
      return index;
    }
  }
  return -1;
}

- (BOOL)isPointInsideMenu:(CGPoint)point {
  return CGRectContainsPoint(menu_frame_, point);
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  (void)event;
  for (UITouch* touch in touches) {
    CGPoint point = [touch locationInView:self];
    NSInteger thumb_index = [self thumbIndexForTouchPoint:point];
    if (thumb_index >= 0 && thumb_touches_[thumb_index] == nil) {
      thumb_touches_[thumb_index] = touch;
      thumb_touch_start_points_[thumb_index] = point;
      thumb_touch_dragged_[thumb_index] = NO;
      [self updateThumbIndex:thumb_index forPoint:point emit:YES];
      continue;
    }

    if ([self isPointInsideMenu:point] && menu_touch_ == nil) {
      menu_touch_ = touch;
      [self setMenuPressed:YES];
      continue;
    }

    NSInteger tag = [self buttonTagForTouchPoint:point];
    if (tag >= 0 && button_touches_[tag] == nil) {
      button_touches_[tag] = touch;
      [self setButtonTag:tag pressed:YES emit:YES];
    }
  }
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  (void)event;
  for (UITouch* touch in touches) {
    CGPoint point = [touch locationInView:self];
    for (NSInteger index = 0; index < 2; ++index) {
      if (thumb_touches_[index] == touch) {
        CGPoint start = thumb_touch_start_points_[index];
        CGFloat dx = point.x - start.x;
        CGFloat dy = point.y - start.y;
        if ((dx * dx + dy * dy) > (12.0f * 12.0f)) {
          thumb_touch_dragged_[index] = YES;
        }
        [self updateThumbIndex:index forPoint:point emit:YES];
        goto next_touch;
      }
    }

    if (menu_touch_ == touch) {
      BOOL inside = [self isPointInsideMenu:point];
      [self setMenuPressed:inside];
      goto next_touch;
    }

    for (NSInteger tag = 1; tag <= 14; ++tag) {
      if (button_touches_[tag] == touch) {
        BOOL inside = CGRectContainsPoint(button_frames_[tag], point);
        [self setButtonTag:tag pressed:inside emit:YES];
        goto next_touch;
      }
    }

  next_touch:;
  }
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  (void)event;
  for (UITouch* touch in touches) {
    CGPoint point = [touch locationInView:self];
    for (NSInteger index = 0; index < 2; ++index) {
      if (thumb_touches_[index] == touch) {
        BOOL should_click = !thumb_touch_dragged_[index] &&
                            CGRectContainsPoint([self thumbHitRectForIndex:index], point);
        thumb_touches_[index] = nil;
        thumb_touch_start_points_[index] = CGPointZero;
        thumb_touch_dragged_[index] = NO;
        [self resetThumbIndex:index emit:YES];
        if (should_click) {
          [self emitThumbClickForIndex:index];
        }
        goto next_touch;
      }
    }

    if (menu_touch_ == touch) {
      BOOL activate = menu_pressed_;
      menu_touch_ = nil;
      [self setMenuPressed:NO];
      if (activate && menuButtonTappedHandler_) {
        menuButtonTappedHandler_();
      }
      goto next_touch;
    }

    for (NSInteger tag = 1; tag <= 14; ++tag) {
      if (button_touches_[tag] == touch) {
        button_touches_[tag] = nil;
        [self setButtonTag:tag pressed:NO emit:YES];
        goto next_touch;
      }
    }

  next_touch:;
  }
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  [self touchesEnded:touches withEvent:event];
}

@end
