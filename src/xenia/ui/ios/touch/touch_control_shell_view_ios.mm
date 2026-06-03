/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#import "xenia/ui/ios/touch/touch_control_shell_view_ios.h"

#include <algorithm>

#import "xenia/ui/ios/shared/ios_theme.h"
#import "xenia/ui/ios/shared/ios_theme_controls.h"
#include "xenia/ui/ios/touch/touch_overlay_style_ios.h"

@implementation XeniaTouchControlShellView {
  UILabel* label_;
  xe::hid::touch::IOSTouchControlDefinition control_;
  BOOL touch_active_;
  BOOL conflict_highlighted_;
  BOOL chrome_suppressed_;
  UIImageView* dpad_arrow_up_;
  UIImageView* dpad_arrow_down_;
  UIImageView* dpad_arrow_left_;
  UIImageView* dpad_arrow_right_;
}

- (instancetype)initWithControl:
    (const xe::hid::touch::IOSTouchControlDefinition&)control {
  if (!(self = [super initWithFrame:CGRectZero])) {
    return nil;
  }

  self.backgroundColor = [UIColor clearColor];
  self.userInteractionEnabled = NO;
  self.layer.borderWidth = 1.5;

  label_ = [[UILabel alloc] initWithFrame:CGRectZero];
  label_.backgroundColor = [UIColor clearColor];
  label_.textAlignment = NSTextAlignmentCenter;
  xe_apply_label_font(label_, UIFontTextStyleCaption1, 12.0,
                      UIFontWeightSemibold);
  label_.textColor = [[UIColor whiteColor] colorWithAlphaComponent:0.92];
  label_.adjustsFontSizeToFitWidth = YES;
  label_.minimumScaleFactor = 0.6f;
  [self addSubview:label_];

  [self applyControlDefinition:control];
  return self;
}

- (void)dealloc {
  [dpad_arrow_right_ release];
  [dpad_arrow_left_ release];
  [dpad_arrow_down_ release];
  [dpad_arrow_up_ release];
  [label_ release];
  [super dealloc];
}

- (void)ensureDpadArrowViews {
  if (dpad_arrow_up_) {
    return;
  }
  UIImageSymbolConfiguration* arrow_config =
      [UIImageSymbolConfiguration configurationWithPointSize:18.0
                                                      weight:UIImageSymbolWeightBold];
  UIImage* up_image =
      [UIImage systemImageNamed:@"chevron.up" withConfiguration:arrow_config];
  UIImage* down_image =
      [UIImage systemImageNamed:@"chevron.down" withConfiguration:arrow_config];
  UIImage* left_image =
      [UIImage systemImageNamed:@"chevron.left" withConfiguration:arrow_config];
  UIImage* right_image =
      [UIImage systemImageNamed:@"chevron.right" withConfiguration:arrow_config];
  dpad_arrow_up_ = [[UIImageView alloc] initWithImage:up_image];
  dpad_arrow_down_ = [[UIImageView alloc] initWithImage:down_image];
  dpad_arrow_left_ = [[UIImageView alloc] initWithImage:left_image];
  dpad_arrow_right_ = [[UIImageView alloc] initWithImage:right_image];
  for (UIImageView* arrow in
       @[ dpad_arrow_up_, dpad_arrow_down_, dpad_arrow_left_, dpad_arrow_right_ ]) {
    arrow.contentMode = UIViewContentModeCenter;
    arrow.userInteractionEnabled = NO;
    arrow.alpha = 0.85f;
    arrow.tintColor = [[UIColor whiteColor] colorWithAlphaComponent:0.92];
    arrow.hidden = YES;
    [self addSubview:arrow];
  }
}

- (BOOL)isMoveDpadComboControl {
  return control_.type == xe::hid::touch::IOSTouchControlType::kMoveStick &&
         control_.move_with_dpad_ring;
}

- (void)applyControlDefinition:
    (const xe::hid::touch::IOSTouchControlDefinition&)control {
  control_ = control;

  NSString* label_text = xe::ui::XeniaTouchVisibleControlLabelText(control_, NO);
  label_.text = label_text;
  label_.hidden = label_text.length == 0;

  if ([self isMoveDpadComboControl]) {
    [self ensureDpadArrowViews];
    UIColor* tint =
        xe::ui::XeniaTouchOverlayAccentColor(control_.tint_style, control_.type);
    for (UIImageView* arrow in
         @[ dpad_arrow_up_, dpad_arrow_down_, dpad_arrow_left_, dpad_arrow_right_ ]) {
      arrow.tintColor = tint;
      arrow.hidden = NO;
    }
  } else {
    dpad_arrow_up_.hidden = YES;
    dpad_arrow_down_.hidden = YES;
    dpad_arrow_left_.hidden = YES;
    dpad_arrow_right_.hidden = YES;
  }

  touch_active_ = NO;
  [self refreshVisualState];
  [self setNeedsLayout];
}

- (CGFloat)baseVisualAlpha {
  return static_cast<CGFloat>(
      control_.type == xe::hid::touch::IOSTouchControlType::kLookSwipeZone
          ? 1.0f
          : std::clamp(control_.visual_opacity, 0.0f, 1.0f));
}

- (void)refreshVisualState {
  if (chrome_suppressed_) {
    [UIView animateWithDuration:0.10
        delay:0.0
        options:(UIViewAnimationOptions)(UIViewAnimationOptionCurveEaseOut |
                                         UIViewAnimationOptionAllowUserInteraction |
                                         UIViewAnimationOptionBeginFromCurrentState)
        animations:^{
          self.alpha = 0.0;
          self.backgroundColor = [UIColor clearColor];
          self.transform = CGAffineTransformIdentity;
        }
        completion:nil];
    [CATransaction begin];
    [CATransaction setAnimationDuration:0.10];
    [CATransaction setAnimationTimingFunction:
                       [CAMediaTimingFunction functionWithName:
                                                 kCAMediaTimingFunctionEaseOut]];
    self.layer.borderColor = [UIColor clearColor].CGColor;
    self.layer.borderWidth = 0.0;
    [CATransaction commit];
    return;
  }

  const CGFloat base_alpha = [self baseVisualAlpha];
  const CGFloat active_alpha = MIN(base_alpha + 0.18f, 1.0f);
  const CGFloat target_alpha = touch_active_ ? active_alpha : base_alpha;
  CGFloat fill_alpha = touch_active_ ? MIN(base_alpha + 0.45f, 1.0f) : base_alpha;
  if (control_.type == xe::hid::touch::IOSTouchControlType::kLookSwipeZone) {
    fill_alpha = touch_active_ ? 0.55f : 0.18f;
  }
  UIColor* target_fill =
      xe::ui::XeniaTouchOverlayFillColorForControl(control_, fill_alpha);
  UIColor* target_border =
      xe::ui::XeniaTouchOverlayBorderColorForControl(control_);
  if (conflict_highlighted_) {
    target_border = [[XeniaTheme statusError] colorWithAlphaComponent:0.95];
  }
  if (touch_active_) {
    target_border =
        control_.type == xe::hid::touch::IOSTouchControlType::kLookSwipeZone
            ? xe::ui::XeniaTouchOverlayAccentColor(control_.tint_style,
                                                   control_.type)
            : [[UIColor whiteColor] colorWithAlphaComponent:0.78];
  }
  const CGFloat target_border_width =
      control_.type == xe::hid::touch::IOSTouchControlType::kLookSwipeZone
          ? (touch_active_ ? 1.8f : 1.0f)
          : (touch_active_ ? 2.0f : 1.5f);
  const CGAffineTransform target_transform =
      touch_active_ ? CGAffineTransformMakeScale(1.03f, 1.03f)
                    : CGAffineTransformIdentity;

  [UIView animateWithDuration:0.08
      delay:0.0
      options:(UIViewAnimationOptions)(UIViewAnimationOptionCurveEaseOut |
                                       UIViewAnimationOptionAllowUserInteraction |
                                       UIViewAnimationOptionBeginFromCurrentState)
      animations:^{
        self.alpha = target_alpha;
        self.backgroundColor = target_fill;
        self.transform = target_transform;
      }
      completion:nil];

  [CATransaction begin];
  [CATransaction setAnimationDuration:0.08];
  [CATransaction setAnimationTimingFunction:
                     [CAMediaTimingFunction functionWithName:
                                               kCAMediaTimingFunctionEaseOut]];
  self.layer.borderColor = target_border.CGColor;
  self.layer.borderWidth = target_border_width;
  [CATransaction commit];
}

- (void)setTouchActive:(BOOL)active {
  if (touch_active_ == active) {
    return;
  }
  touch_active_ = active;
  [self refreshVisualState];
}

- (void)setConflictHighlighted:(BOOL)highlighted {
  if (conflict_highlighted_ == highlighted) {
    return;
  }
  conflict_highlighted_ = highlighted;
  [self refreshVisualState];
}

- (void)setChromeSuppressed:(BOOL)suppressed {
  if (chrome_suppressed_ == suppressed) {
    return;
  }
  chrome_suppressed_ = suppressed;
  label_.hidden = suppressed || (label_.text.length == 0);
  [self refreshVisualState];
}

- (void)layoutSubviews {
  [super layoutSubviews];

  CGFloat corner_radius = 16.0f;
  if (control_.shape == xe::hid::touch::IOSTouchControlShape::kCircle) {
    corner_radius =
        MIN(CGRectGetWidth(self.bounds), CGRectGetHeight(self.bounds)) * 0.5f;
  } else if (control_.type ==
             xe::hid::touch::IOSTouchControlType::kLookSwipeZone) {
    corner_radius = 22.0f;
  }
  self.layer.cornerRadius = corner_radius;

  label_.frame = CGRectInset(self.bounds, 8.0f, 8.0f);
  if (control_.type == xe::hid::touch::IOSTouchControlType::kLookSwipeZone) {
    xe_apply_label_font(label_, UIFontTextStyleCaption2, 11.0,
                        UIFontWeightMedium);
    label_.textColor = [[UIColor whiteColor] colorWithAlphaComponent:0.30];
  } else {
    xe_apply_label_font(label_, UIFontTextStyleCaption1, 12.0,
                        UIFontWeightSemibold);
    label_.textColor = [[UIColor whiteColor] colorWithAlphaComponent:0.92];
  }

  if ([self isMoveDpadComboControl] && dpad_arrow_up_) {
    const CGFloat short_side =
        MIN(CGRectGetWidth(self.bounds), CGRectGetHeight(self.bounds));
    const CGFloat stick_radius =
        short_side * xe::ui::kXeniaTouchComboStickRadiusFraction;
    const CGFloat outer_radius = short_side * 0.5f;
    const CGFloat arrow_radius = (stick_radius + outer_radius) * 0.5f;
    const CGFloat arrow_size = MAX(short_side * 0.18f, 24.0f);
    const CGFloat centre_x = CGRectGetMidX(self.bounds);
    const CGFloat centre_y = CGRectGetMidY(self.bounds);
    dpad_arrow_up_.frame =
        CGRectMake(centre_x - arrow_size * 0.5f,
                   centre_y - arrow_radius - arrow_size * 0.5f, arrow_size,
                   arrow_size);
    dpad_arrow_down_.frame =
        CGRectMake(centre_x - arrow_size * 0.5f,
                   centre_y + arrow_radius - arrow_size * 0.5f, arrow_size,
                   arrow_size);
    dpad_arrow_left_.frame =
        CGRectMake(centre_x - arrow_radius - arrow_size * 0.5f,
                   centre_y - arrow_size * 0.5f, arrow_size, arrow_size);
    dpad_arrow_right_.frame =
        CGRectMake(centre_x + arrow_radius - arrow_size * 0.5f,
                   centre_y - arrow_size * 0.5f, arrow_size, arrow_size);
  }
}

@end
