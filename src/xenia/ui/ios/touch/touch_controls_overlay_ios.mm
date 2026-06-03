/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#import "xenia/ui/ios/touch/touch_controls_overlay_ios.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "xenia/base/cvar.h"
#include "xenia/hid/touch/touch_layout_editor.h"
#include "xenia/hid/touch/touch_layout_ios.h"
#import "xenia/ui/ios/shared/ios_theme_controls.h"
#import "xenia/ui/ios/shared/ios_view_helpers.h"
#import "xenia/ui/ios/touch/touch_control_shell_view_ios.h"
#import "xenia/ui/ios/touch/touch_controls_overlay_helpers_ios.h"
#import "xenia/ui/ios/touch/touch_layout_library_view_ios.h"
#import "xenia/ui/ios/touch/touch_overlay_edit_chrome_ios.h"
#include "xenia/ui/ios/touch/touch_overlay_edit_history_ios.h"
#include "xenia/ui/ios/touch/touch_overlay_geometry_ios.h"
#include "xenia/ui/ios/touch/touch_overlay_style_ios.h"

DECLARE_bool(ios_touch_haptics);

namespace {

using xe::ui::CGRectFromTouchRect;
using xe::ui::ClampNormalizedControlFrame;
using xe::ui::NormalizedControlFrameFromResolvedFrame;
using xe::ui::ResolveNormalizedControlFrame;
using xe::ui::SnapTouchEditResolvedFrame;
using xe::ui::TouchControlDeckSpaceForView;
using xe::ui::TouchControlSizeSpaceForControlType;
using xe::ui::TouchEditGestureMode;
using xe::ui::TouchEditSnapOptions;
using xe::ui::TouchEditSnapResult;
using xe::ui::TouchOverlayIsPortraitForView;
using xe::ui::TouchSafeAreaSpaceForView;
using xe::ui::XeniaTouchConfiguredControlLabelText;
using xe::ui::XeniaTouchVisibleControlLabelText;

using namespace xe::ui::ios::touch_overlay;

}  // namespace

@interface XeniaTouchControlsOverlayView () <XeniaTouchOverlayEditChromeIOSDelegate>

- (void)finalizeTouches:(NSSet<UITouch*>*)touches cancelled:(BOOL)cancelled;

@end

@implementation XeniaTouchControlsOverlayView {
  xe::hid::touch::IOSTouchRuntimeModel* runtime_model_;
  NSMutableArray<XeniaTouchControlShellView*>* control_views_;
  UIButton* pause_button_;
  UIView* edit_grid_overlay_;
  CAShapeLayer* edit_grid_dots_layer_;
  UIView* edit_snap_guides_overlay_;
  CAShapeLayer* edit_snap_guides_layer_;
  UIView* edit_safe_area_guide_;
  XeniaTouchOverlayEditChromeIOS* edit_chrome_;
  UIView* edit_resize_handle_;
  UIView* move_knob_;
  CADisplayLink* display_link_;
  std::vector<xe::hid::touch::IOSTouchRect> resolved_control_frames_;
  std::vector<bool> conflicting_control_indices_;
  std::vector<uint8_t> visually_active_control_indices_;
  std::vector<CFTimeInterval> recent_action_press_times_;
  std::vector<CFTimeInterval> recent_secondary_press_times_;
  std::vector<CFTimeInterval> recent_secondary_candidate_times_;
  std::vector<CGPoint> recent_look_vectors_;
  std::vector<CFTimeInterval> recent_look_motion_times_;
  std::vector<TouchCaptureState> active_captures_;
  std::vector<CGFloat> active_snap_vertical_guides_;
  std::vector<CGFloat> active_snap_horizontal_guides_;
  xe::hid::touch::IOSTouchResolvedState last_published_state_;
  uint32_t next_packet_number_;
  BOOL gameplay_overlay_active_;
  BOOL editing_controls_enabled_;
  BOOL edit_showing_layout_library_;
  NSUInteger selected_control_index_;
  NSUInteger move_control_index_;
  NSUInteger look_control_index_;
  NSUInteger pause_control_index_;
  BOOL edit_grid_enabled_;
  TouchOverlayEditHistoryIOS edit_history_;
  BOOL edit_chrome_minimized_;
  BOOL edit_chrome_drag_active_;
  UITouch* edit_chrome_drag_touch_;
  CGRect edit_chrome_drag_frame_;
  CGPoint edit_chrome_drag_touch_offset_;
  NSInteger edit_chrome_dock_index_;
  BOOL edit_pinch_active_;
  NSUInteger edit_pinch_control_index_;
  UITouch* edit_pinch_touch_a_;
  UITouch* edit_pinch_touch_b_;
  CGFloat edit_pinch_initial_distance_;
  xe::hid::touch::IOSTouchRect edit_pinch_initial_frame_;
  UIImpactFeedbackGenerator* haptic_press_;        // medium impact on action press
  UIImpactFeedbackGenerator* haptic_press_light_;  // light impact on stick engage
  UIImpactFeedbackGenerator* haptic_snap_;         // rigid impact on snap engage
  UISelectionFeedbackGenerator* haptic_selection_;
  BOOL snap_guides_were_visible_;
  // Tracks the orientation we last laid out in. layoutSubviews compares this
  // against the current bounds-derived orientation so it can refresh the
  // edit chrome chip when the device rotates without forcing a refresh on
  // every layout pass.
  BOOL last_layout_was_portrait_;
  BOOL last_layout_orientation_known_;
  UIView* tooltip_view_;
  UILabel* tooltip_label_;
  UILongPressGestureRecognizer* tooltip_long_press_;
}

@synthesize pauseHandler = pauseHandler_;
@synthesize doneEditingHandler = doneEditingHandler_;
@synthesize layoutLibraryHandler = layoutLibraryHandler_;
@synthesize layoutLibraryLoadHandler = layoutLibraryLoadHandler_;
@synthesize layoutLibrarySaveCopyHandler = layoutLibrarySaveCopyHandler_;
@synthesize layoutLibraryRenameHandler = layoutLibraryRenameHandler_;
@synthesize layoutLibraryDeleteHandler = layoutLibraryDeleteHandler_;
@synthesize layoutLibraryImportHandler = layoutLibraryImportHandler_;
@synthesize layoutLibraryExportHandler = layoutLibraryExportHandler_;
@synthesize layoutLibraryResetHandler = layoutLibraryResetHandler_;

- (void)createDisplayLinkIfNeeded {
  if (display_link_) {
    return;
  }
  display_link_ = [[CADisplayLink displayLinkWithTarget:self
                                               selector:@selector(displayLinkFired:)] retain];
  display_link_.paused = YES;
  [display_link_ addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (instancetype)initWithRuntimeModel:(xe::hid::touch::IOSTouchRuntimeModel*)runtime_model {
  if (!(self = [super initWithFrame:CGRectZero])) {
    return nil;
  }

  // The touch overlay sits on top of running gameplay; force dark so pause
  // glyphs, edit chrome, tooltips, and snap guides stay readable.
  self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;

  runtime_model_ = runtime_model;
  control_views_ = [[NSMutableArray alloc] init];
  move_control_index_ = NSNotFound;
  look_control_index_ = NSNotFound;
  pause_control_index_ = NSNotFound;
  next_packet_number_ = 1;
  gameplay_overlay_active_ = NO;
  editing_controls_enabled_ = NO;
  edit_showing_layout_library_ = NO;
  selected_control_index_ = NSNotFound;
  edit_chrome_minimized_ = NO;
  edit_chrome_drag_active_ = NO;
  edit_chrome_drag_touch_ = nil;
  edit_chrome_drag_frame_ = CGRectZero;
  edit_chrome_drag_touch_offset_ = CGPointZero;
  edit_chrome_dock_index_ = kEditChromeDockAuto;
  last_layout_was_portrait_ = NO;
  last_layout_orientation_known_ = NO;

  pause_button_ = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
  pause_button_.backgroundColor = [UIColor clearColor];
  pause_button_.hidden = YES;
  pause_button_.accessibilityLabel = @"Pause game";
  pause_button_.accessibilityTraits = UIAccessibilityTraitButton;
  [pause_button_ addTarget:self
                    action:@selector(pauseButtonPressed:)
          forControlEvents:UIControlEventTouchUpInside];
  [self addSubview:pause_button_];

  edit_grid_overlay_ = [[UIView alloc] initWithFrame:CGRectZero];
  edit_grid_overlay_.hidden = YES;
  edit_grid_overlay_.backgroundColor = [UIColor clearColor];
  edit_grid_overlay_.userInteractionEnabled = NO;
  edit_grid_dots_layer_ = [[CAShapeLayer alloc] init];
  edit_grid_dots_layer_.fillColor =
      [[UIColor whiteColor] colorWithAlphaComponent:[XeniaTheme opacitySoft]].CGColor;
  edit_grid_dots_layer_.strokeColor = nil;
  [edit_grid_overlay_.layer addSublayer:edit_grid_dots_layer_];
  [self addSubview:edit_grid_overlay_];

  edit_snap_guides_overlay_ = [[UIView alloc] initWithFrame:CGRectZero];
  edit_snap_guides_overlay_.hidden = YES;
  edit_snap_guides_overlay_.backgroundColor = [UIColor clearColor];
  edit_snap_guides_overlay_.userInteractionEnabled = NO;
  edit_snap_guides_layer_ = [[CAShapeLayer alloc] init];
  edit_snap_guides_layer_.hidden = YES;
  edit_snap_guides_layer_.fillColor = nil;
  edit_snap_guides_layer_.strokeColor =
      [[XeniaTheme touchTintAmber] colorWithAlphaComponent:0.88].CGColor;
  edit_snap_guides_layer_.lineWidth = kEditSnapGuideLineWidth;
  edit_snap_guides_layer_.lineDashPattern = @[ @5.0f, @7.0f ];
  edit_snap_guides_layer_.lineCap = kCALineCapRound;
  [edit_snap_guides_overlay_.layer addSublayer:edit_snap_guides_layer_];
  [self addSubview:edit_snap_guides_overlay_];

  edit_safe_area_guide_ = [[UIView alloc] initWithFrame:CGRectZero];
  edit_safe_area_guide_.hidden = YES;
  edit_safe_area_guide_.backgroundColor = [UIColor clearColor];
  edit_safe_area_guide_.layer.borderWidth = 1.0;
  edit_safe_area_guide_.layer.borderColor =
      [[UIColor whiteColor] colorWithAlphaComponent:[XeniaTheme opacityStrong]].CGColor;
  // Matches xe_apply_floating_window_chrome's 20pt — visually pairs the safe
  // area indicator with the floating editor chrome it sits behind.
  edit_safe_area_guide_.layer.cornerRadius = 20.0;
  edit_safe_area_guide_.userInteractionEnabled = NO;
  [self addSubview:edit_safe_area_guide_];

  edit_chrome_ = [[XeniaTouchOverlayEditChromeIOS alloc] initWithFrame:CGRectZero];
  edit_chrome_.delegate = self;
  [self addSubview:edit_chrome_];

  edit_resize_handle_ = [[UIView alloc] initWithFrame:CGRectZero];
  edit_resize_handle_.hidden = YES;
  edit_resize_handle_.backgroundColor =
      [[XeniaTheme touchTintAmber] colorWithAlphaComponent:0.92];
  // Resize handle is a 28pt-diameter visual element; 14pt = 28/2 keeps the
  // circle a true circle even when the handle is offset off-corner.
  edit_resize_handle_.layer.cornerRadius = 14.0;
  edit_resize_handle_.layer.borderWidth = 1.5;
  edit_resize_handle_.layer.borderColor =
      [[UIColor blackColor] colorWithAlphaComponent:0.35].CGColor;
  [self addSubview:edit_resize_handle_];

  move_knob_ = [[UIView alloc] initWithFrame:CGRectZero];
  move_knob_.hidden = YES;
  move_knob_.backgroundColor =
      [[UIColor whiteColor] colorWithAlphaComponent:0.18];
  move_knob_.layer.borderWidth = 1.0;
  move_knob_.layer.borderColor =
      [[UIColor whiteColor] colorWithAlphaComponent:0.72].CGColor;
  [self addSubview:move_knob_];

  [self createDisplayLinkIfNeeded];

  self.backgroundColor = [UIColor clearColor];
  self.opaque = NO;
  self.alpha = 0.0;
  self.hidden = YES;
  self.userInteractionEnabled = YES;
  self.multipleTouchEnabled = YES;
  edit_grid_enabled_ = NO;

  // Pre-construct haptic generators so the first press does not pay the
  // generator-init latency. The system frees them when nothing has used them
  // recently; -prepare on each fire keeps them warm during gameplay.
  haptic_press_ = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleMedium];
  haptic_press_light_ =
      [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
  haptic_snap_ = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleRigid];
  haptic_selection_ = [[UISelectionFeedbackGenerator alloc] init];

  // Long-press tooltip: when the user holds a finger on a control in edit
  // mode for ~0.4s, show a bubble describing what the control is bound to.
  // Helps with discoverability without forcing the user into the binding menu.
  // Use the unified floating-window chrome so all of the iOS chrome shares the
  // same look.
  tooltip_view_ = [[UIView alloc] initWithFrame:CGRectZero];
  xe_apply_floating_window_chrome(tooltip_view_);
  tooltip_view_.alpha = 0.0f;
  tooltip_view_.userInteractionEnabled = NO;
  tooltip_view_.hidden = YES;
  [self addSubview:tooltip_view_];

  tooltip_label_ = [[UILabel alloc] initWithFrame:CGRectZero];
  tooltip_label_.numberOfLines = 0;
  tooltip_label_.textColor = [UIColor whiteColor];
  xe_apply_label_font(tooltip_label_, UIFontTextStyleCaption1, 12.0,
                      UIFontWeightMedium);
  tooltip_label_.textAlignment = NSTextAlignmentLeft;
  tooltip_label_.backgroundColor = [UIColor clearColor];
  [tooltip_view_ addSubview:tooltip_label_];

  tooltip_long_press_ =
      [[UILongPressGestureRecognizer alloc] initWithTarget:self
                                                    action:@selector(tooltipLongPressTriggered:)];
  tooltip_long_press_.minimumPressDuration = 0.40;
  tooltip_long_press_.cancelsTouchesInView = NO;
  tooltip_long_press_.delaysTouchesBegan = NO;
  tooltip_long_press_.delaysTouchesEnded = NO;
  [self addGestureRecognizer:tooltip_long_press_];

  [self refreshLayoutModel];
  return self;
}

- (void)playPressHaptic {
  if (!cvars::ios_touch_haptics) {
    return;
  }
  [haptic_press_ impactOccurred];
  [haptic_press_ prepare];
}

- (void)playLightPressHaptic {
  if (!cvars::ios_touch_haptics) {
    return;
  }
  [haptic_press_light_ impactOccurred];
  [haptic_press_light_ prepare];
}

- (void)playSnapHaptic {
  if (!cvars::ios_touch_haptics) {
    return;
  }
  [haptic_snap_ impactOccurred];
  [haptic_snap_ prepare];
}

- (void)playSelectionHaptic {
  if (!cvars::ios_touch_haptics) {
    return;
  }
  [haptic_selection_ selectionChanged];
  [haptic_selection_ prepare];
}

#pragma mark - Hardware keyboard shortcuts (edit mode)

- (BOOL)canBecomeFirstResponder {
  return editing_controls_enabled_;
}

- (NSArray<UIKeyCommand*>*)keyCommands {
  if (!editing_controls_enabled_) {
    return @[];
  }
  UIKeyCommand* undo = [UIKeyCommand keyCommandWithInput:@"z"
                                           modifierFlags:UIKeyModifierCommand
                                                  action:@selector(keyCommandUndo:)];
  undo.discoverabilityTitle = @"Undo edit";
  UIKeyCommand* redo = [UIKeyCommand keyCommandWithInput:@"z"
                                           modifierFlags:UIKeyModifierCommand | UIKeyModifierShift
                                                  action:@selector(keyCommandRedo:)];
  redo.discoverabilityTitle = @"Redo edit";
  UIKeyCommand* duplicate = [UIKeyCommand keyCommandWithInput:@"d"
                                                modifierFlags:UIKeyModifierCommand
                                                       action:@selector(keyCommandDuplicate:)];
  duplicate.discoverabilityTitle = @"Duplicate selected control";
  UIKeyCommand* mirror = [UIKeyCommand keyCommandWithInput:@"m"
                                             modifierFlags:UIKeyModifierCommand
                                                    action:@selector(keyCommandMirror:)];
  mirror.discoverabilityTitle = @"Mirror selected control horizontally";
  UIKeyCommand* delete_command = [UIKeyCommand keyCommandWithInput:@"\b"
                                                     modifierFlags:UIKeyModifierCommand
                                                            action:@selector(keyCommandDelete:)];
  delete_command.discoverabilityTitle = @"Delete selected control";
  UIKeyCommand* done = [UIKeyCommand keyCommandWithInput:UIKeyInputEscape
                                           modifierFlags:0
                                                  action:@selector(keyCommandDone:)];
  done.discoverabilityTitle = @"Exit edit mode";
  return @[ undo, redo, duplicate, mirror, delete_command, done ];
}

- (void)keyCommandUndo:(UIKeyCommand*)__unused command {
  [self undoEditLayoutChange];
}

- (void)keyCommandRedo:(UIKeyCommand*)__unused command {
  [self redoEditLayoutChange];
}

- (void)keyCommandDuplicate:(UIKeyCommand*)__unused command {
  [self duplicateSelectedControl];
}

- (void)keyCommandMirror:(UIKeyCommand*)__unused command {
  [self mirrorSelectedControlHorizontally];
}

- (void)keyCommandDelete:(UIKeyCommand*)__unused command {
  [self deleteSelectedControl];
}

- (void)keyCommandDone:(UIKeyCommand*)__unused command {
  if (doneEditingHandler_) {
    doneEditingHandler_();
  }
}

#pragma mark - Long-press tooltip

- (void)hideTooltip {
  if (tooltip_view_.hidden) {
    return;
  }
  [UIView animateWithDuration:0.10
      animations:^{
        tooltip_view_.alpha = 0.0;
      }
      completion:^(__unused BOOL finished) {
        tooltip_view_.hidden = YES;
      }];
}

- (void)showTooltipForControlAtIndex:(NSUInteger)control_index nearPoint:(CGPoint)point {
  if (!runtime_model_) {
    return;
  }
  const auto& controls = runtime_model_->layout().controls;
  if (control_index >= controls.size()) {
    return;
  }
  const xe::hid::touch::IOSTouchControlDefinition& control = controls[control_index];

  NSMutableString* text = [NSMutableString string];
  NSString* label = XeniaTouchVisibleControlLabelText(control, NO);
  if (label.length) {
    [text appendFormat:@"%@\n", label];
  }
  [text appendFormat:@"Action: %s", xe::hid::touch::IOSTouchActionDisplayName(control.action)];
  if (control.secondary_behavior.trigger != xe::hid::touch::IOSTouchInteractionTrigger::kNone &&
      control.secondary_behavior.action != xe::hid::touch::IOSTouchAction::kNone) {
    [text
        appendFormat:@"\n2nd: %s · %s",
                     xe::hid::touch::IOSTouchInteractionTriggerDisplayName(
                         control.secondary_behavior.trigger),
                     xe::hid::touch::IOSTouchActionDisplayName(control.secondary_behavior.action)];
  }
  [text
      appendFormat:@"\nTint: %s", xe::hid::touch::IOSTouchTintStyleDisplayName(control.tint_style)];
  const bool tooltip_is_portrait = TouchOverlayIsPortraitForView(self);
  const xe::hid::touch::IOSTouchRect& tooltip_frame =
      xe::hid::touch::ActiveControlFrameForOrientation(control, tooltip_is_portrait);
  [text appendFormat:@"\nFrame: %.2f, %.2f · %.2f × %.2f", tooltip_frame.x, tooltip_frame.y,
                     tooltip_frame.width, tooltip_frame.height];

  tooltip_label_.text = text;
  const CGSize max_size = CGSizeMake(220.0f, 1000.0f);
  CGSize content_size = [tooltip_label_ sizeThatFits:max_size];
  const CGFloat tooltip_padding_x = 12.0f;
  const CGFloat tooltip_padding_y = 10.0f;
  const CGFloat tooltip_width = ceilf(content_size.width) + tooltip_padding_x * 2.0f;
  const CGFloat tooltip_height = ceilf(content_size.height) + tooltip_padding_y * 2.0f;

  // Position above the press point if there's room; otherwise below.
  CGFloat origin_x = std::clamp<CGFloat>(point.x - tooltip_width * 0.5, 8.0,
                                         CGRectGetWidth(self.bounds) - tooltip_width - 8.0);
  CGFloat origin_y = point.y - tooltip_height - 18.0;
  if (origin_y < self.safeAreaInsets.top + 8.0) {
    origin_y = point.y + 18.0;
  }
  origin_y = std::clamp<CGFloat>(
      origin_y, self.safeAreaInsets.top + 8.0,
      CGRectGetHeight(self.bounds) - tooltip_height - self.safeAreaInsets.bottom - 8.0);

  tooltip_view_.frame = CGRectMake(origin_x, origin_y, tooltip_width, tooltip_height);
  tooltip_label_.frame =
      CGRectMake(tooltip_padding_x, tooltip_padding_y, content_size.width, content_size.height);

  [self bringSubviewToFront:tooltip_view_];
  if (tooltip_view_.hidden) {
    tooltip_view_.alpha = 0.0;
    tooltip_view_.hidden = NO;
  }
  [UIView animateWithDuration:0.12
                   animations:^{
                     tooltip_view_.alpha = 1.0;
                   }];
}

- (void)tooltipLongPressTriggered:(UILongPressGestureRecognizer*)recognizer {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }
  if (recognizer.state == UIGestureRecognizerStateBegan) {
    const CGPoint point = [recognizer locationInView:self];
    const auto& controls = runtime_model_->layout().controls;
    const NSUInteger control_count =
        MIN(control_views_.count, static_cast<NSUInteger>(controls.size()));
    NSInteger best_index = -1;
    uint8_t best_priority = 0;
    for (NSUInteger control_index = 0; control_index < control_count; ++control_index) {
      if (control_index >= resolved_control_frames_.size()) {
        continue;
      }
      const xe::hid::touch::IOSTouchControlDefinition& control = controls[control_index];
      if (!TouchControlContainsPoint(control, resolved_control_frames_[control_index], point)) {
        continue;
      }
      if (best_index < 0 || control.capture_priority > best_priority) {
        best_index = static_cast<NSInteger>(control_index);
        best_priority = control.capture_priority;
      }
    }
    if (best_index >= 0) {
      [self showTooltipForControlAtIndex:static_cast<NSUInteger>(best_index) nearPoint:point];
    }
  } else if (recognizer.state == UIGestureRecognizerStateEnded ||
             recognizer.state == UIGestureRecognizerStateCancelled ||
             recognizer.state == UIGestureRecognizerStateFailed) {
    [self hideTooltip];
  }
}

- (void)willMoveToWindow:(UIWindow*)new_window {
  if (!new_window && display_link_) {
    [display_link_ invalidate];
    [display_link_ release];
    display_link_ = nil;
  } else if (new_window && !display_link_) {
    [self createDisplayLinkIfNeeded];
  }
  [super willMoveToWindow:new_window];
}

- (void)dealloc {
  [display_link_ invalidate];
  [display_link_ release];
  [haptic_press_ release];
  [haptic_press_light_ release];
  [haptic_snap_ release];
  [haptic_selection_ release];
  [tooltip_long_press_ release];
  [tooltip_label_ release];
  [tooltip_view_ release];
  [layoutLibraryResetHandler_ release];
  [layoutLibraryExportHandler_ release];
  [layoutLibraryImportHandler_ release];
  [layoutLibraryDeleteHandler_ release];
  [layoutLibraryRenameHandler_ release];
  [layoutLibrarySaveCopyHandler_ release];
  [layoutLibraryLoadHandler_ release];
  [layoutLibraryHandler_ release];
  [doneEditingHandler_ release];
  [pauseHandler_ release];
  [pause_button_ release];
  [edit_snap_guides_layer_ release];
  [edit_snap_guides_overlay_ release];
  [edit_grid_dots_layer_ release];
  [edit_grid_overlay_ release];
  [edit_resize_handle_ release];
  edit_chrome_.delegate = nil;
  [edit_chrome_ release];
  [edit_safe_area_guide_ release];
  [move_knob_ release];
  [control_views_ release];
  [super dealloc];
}

- (BOOL)isShowingLayoutLibrary {
  return edit_showing_layout_library_;
}

- (void)showLayoutLibraryWithItems:(NSArray<XeniaTouchLayoutLibraryItem*>*)items
              currentLayoutLocalID:(NSString*)currentLayoutLocalID {
  [edit_chrome_ setLayoutLibraryItems:items currentLayoutLocalID:currentLayoutLocalID];
  edit_showing_layout_library_ = YES;
  edit_chrome_minimized_ = NO;
  [self refreshEditChromeSelection];
  [self setNeedsLayout];
}

- (void)hideLayoutLibrary {
  edit_showing_layout_library_ = NO;
  [self refreshEditChromeSelection];
  [self setNeedsLayout];
}

- (std::string)selectedControlIdentifier {
  if (!runtime_model_ || selected_control_index_ == NSNotFound) {
    return std::string();
  }
  const auto& controls = runtime_model_->layout().controls;
  if (selected_control_index_ >= controls.size()) {
    return std::string();
  }
  return controls[selected_control_index_].identifier;
}

- (void)resetEditLayoutHistory {
  edit_history_.Reset();
}

- (void)seedEditLayoutHistoryIfNeeded {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }
  edit_history_.SeedIfNeeded(runtime_model_->layout(), [self selectedControlIdentifier]);
}

- (BOOL)canUndoEditLayoutChange {
  return edit_history_.CanUndo();
}

- (BOOL)canRedoEditLayoutChange {
  return edit_history_.CanRedo();
}

- (void)applyEditLayoutHistoryState:(const xe::hid::touch::IOSTouchLayoutModel&)layout
                selectingIdentifier:(const std::string&)preferred_identifier {
  if (!runtime_model_) {
    return;
  }

  edit_history_.CancelChange();
  [self clearEditSnapGuides];
  runtime_model_->SetLayout(layout);
  [self refreshLayoutModel];
  if (!preferred_identifier.empty()) {
    [self selectControlWithIdentifier:preferred_identifier];
  }
  [self publishResolvedState];
}

- (void)beginEditLayoutChangeIfNeeded {
  if (!editing_controls_enabled_ || !runtime_model_ || edit_history_.IsChangeActive()) {
    return;
  }
  edit_history_.BeginChange(runtime_model_->layout(), [self selectedControlIdentifier]);
}

- (void)finishEditLayoutChangeIfNeeded {
  if (!edit_history_.IsChangeActive() || !runtime_model_) {
    return;
  }
  const BOOL no_active_gestures = active_captures_.empty() && !edit_pinch_active_;
  if (!no_active_gestures) {
    return;
  }
  if (edit_history_.FinishChange(runtime_model_->layout(), [self selectedControlIdentifier])) {
    [self refreshEditChromeSelection];
  }
}

- (void)undoEditLayoutChange {
  if (![self canUndoEditLayoutChange] || !runtime_model_ || !active_captures_.empty() ||
      edit_pinch_active_) {
    return;
  }
  xe::hid::touch::IOSTouchLayoutModel layout;
  std::string preferred_identifier;
  if (!edit_history_.Undo(&layout, &preferred_identifier)) {
    return;
  }
  [self applyEditLayoutHistoryState:layout selectingIdentifier:preferred_identifier];
  [self refreshEditChromeSelection];
}

- (void)redoEditLayoutChange {
  if (![self canRedoEditLayoutChange] || !runtime_model_ || !active_captures_.empty() ||
      edit_pinch_active_) {
    return;
  }
  xe::hid::touch::IOSTouchLayoutModel layout;
  std::string preferred_identifier;
  if (!edit_history_.Redo(&layout, &preferred_identifier)) {
    return;
  }
  [self applyEditLayoutHistoryState:layout selectingIdentifier:preferred_identifier];
  [self refreshEditChromeSelection];
}

- (void)refreshLayoutModel {
  std::string selected_identifier;
  if (runtime_model_) {
    const auto& existing_controls = runtime_model_->layout().controls;
    if (selected_control_index_ != NSNotFound &&
        selected_control_index_ < existing_controls.size()) {
      selected_identifier = existing_controls[selected_control_index_].identifier;
    }
  }
  [self resetInteractionState];
  for (UIView* control_view in control_views_) {
    [control_view removeFromSuperview];
  }
  [control_views_ removeAllObjects];
  resolved_control_frames_.clear();
  conflicting_control_indices_.clear();
  visually_active_control_indices_.clear();
  recent_action_press_times_.clear();
  recent_secondary_press_times_.clear();
  recent_secondary_candidate_times_.clear();
  recent_look_vectors_.clear();
  recent_look_motion_times_.clear();
  move_control_index_ = NSNotFound;
  look_control_index_ = NSNotFound;
  pause_control_index_ = NSNotFound;
  selected_control_index_ = NSNotFound;

  if (!runtime_model_) {
    pause_button_.hidden = YES;
    move_knob_.hidden = YES;
    return;
  }

  const auto& controls = runtime_model_->layout().controls;
  resolved_control_frames_.resize(controls.size());
  conflicting_control_indices_.assign(controls.size(), false);
  visually_active_control_indices_.assign(controls.size(), 0);
  recent_action_press_times_.assign(controls.size(), 0.0);
  recent_secondary_press_times_.assign(controls.size(), 0.0);
  recent_secondary_candidate_times_.assign(controls.size(), 0.0);
  recent_look_vectors_.assign(controls.size(), CGPointZero);
  recent_look_motion_times_.assign(controls.size(), 0.0);
  NSUInteger control_index = 0;
  for (const auto& control : controls) {
    XeniaTouchControlShellView* shell_view =
        [[XeniaTouchControlShellView alloc] initWithControl:control];
    [control_views_ addObject:shell_view];
    [self addSubview:shell_view];
    [shell_view release];
    switch (control.type) {
      case xe::hid::touch::IOSTouchControlType::kMoveStick:
        move_control_index_ = control_index;
        break;
      case xe::hid::touch::IOSTouchControlType::kLookSwipeZone:
        look_control_index_ = control_index;
        break;
      case xe::hid::touch::IOSTouchControlType::kPauseButton:
        pause_control_index_ = control_index;
        break;
      case xe::hid::touch::IOSTouchControlType::kActionButton:
      default:
        break;
    }
    ++control_index;
  }

  pause_button_.hidden = pause_control_index_ == NSNotFound;
  move_knob_.hidden = YES;
  edit_grid_overlay_.hidden = !editing_controls_enabled_ || !edit_grid_enabled_;
  edit_snap_guides_overlay_.hidden =
      !editing_controls_enabled_ ||
      (active_snap_vertical_guides_.empty() && active_snap_horizontal_guides_.empty());
  edit_safe_area_guide_.hidden = !editing_controls_enabled_;
  edit_chrome_.hidden = !editing_controls_enabled_;
  edit_resize_handle_.hidden = !editing_controls_enabled_;
  [self bringSubviewToFront:edit_safe_area_guide_];
  [self bringSubviewToFront:edit_snap_guides_overlay_];
  [self bringSubviewToFront:edit_chrome_];
  [self bringSubviewToFront:edit_resize_handle_];
  [self bringSubviewToFront:move_knob_];
  [self bringSubviewToFront:pause_button_];

  if (!selected_identifier.empty()) {
    const NSUInteger control_count =
        static_cast<NSUInteger>(runtime_model_->layout().controls.size());
    for (NSUInteger control_index = 0; control_index < control_count; ++control_index) {
      if (runtime_model_->layout().controls[control_index].identifier == selected_identifier) {
        [self setSelectedControlIndex:control_index];
        break;
      }
    }
  }
  if (editing_controls_enabled_ && selected_control_index_ == NSNotFound && runtime_model_ &&
      !runtime_model_->layout().controls.empty()) {
    [self setSelectedControlIndex:(move_control_index_ != NSNotFound ? move_control_index_ : 0)];
  } else {
    [self refreshEditChromeSelection];
    [self refreshEditPreview];
  }
  [self setNeedsLayout];
}

- (BOOL)isControlIndexCaptured:(NSUInteger)control_index {
  return std::any_of(active_captures_.begin(), active_captures_.end(),
                     [control_index](const TouchCaptureState& capture) {
                       return capture.control_index == control_index;
                     });
}

- (TouchOverlayEditChromeState)currentEditChromeState {
  TouchOverlayEditChromeState state;
  state.editing_enabled = editing_controls_enabled_;
  state.showing_layout_library = edit_showing_layout_library_;
  state.minimized = edit_chrome_minimized_;
  state.grid_enabled = edit_grid_enabled_;
  state.can_undo = [self canUndoEditLayoutChange];
  state.can_redo = [self canRedoEditLayoutChange];
  state.editing_portrait = TouchOverlayIsPortraitForView(self);

  if (!runtime_model_) {
    return state;
  }

  const auto& controls = runtime_model_->layout().controls;
  state.control_count = controls.size();
  state.layout_contains_move_stick =
      [self layoutContainsControlType:xe::hid::touch::IOSTouchControlType::kMoveStick];
  state.layout_contains_look_zone =
      [self layoutContainsControlType:xe::hid::touch::IOSTouchControlType::kLookSwipeZone];
  state.layout_contains_pause_button =
      [self layoutContainsControlType:xe::hid::touch::IOSTouchControlType::kPauseButton];
  if (selected_control_index_ != NSNotFound && selected_control_index_ < controls.size()) {
    state.has_selected_control = true;
    state.selected_control = controls[selected_control_index_];
    state.can_match_selected_size =
        [self nearestSizeMatchControlIndexForSelectedControl] != NSNotFound;
  }
  return state;
}

- (void)refreshEditChromeSelection {
  [edit_chrome_ applyState:[self currentEditChromeState]];
  [self setNeedsLayout];
}

- (CGRect)selectedControlResizeHandleFrame {
  if (!editing_controls_enabled_ || selected_control_index_ == NSNotFound ||
      selected_control_index_ >= resolved_control_frames_.size()) {
    return CGRectZero;
  }

  const xe::hid::touch::IOSTouchRect& frame = resolved_control_frames_[selected_control_index_];
  const CGFloat handle_size = 28.0f;
  return CGRectMake(frame.x + frame.width - handle_size * 0.5f,
                    frame.y + frame.height - handle_size * 0.5f, handle_size, handle_size);
}

- (void)refreshEditPreview {
  [edit_chrome_ applyState:[self currentEditChromeState]];
  [self setNeedsLayout];
}

- (float)doubleTapWindowSecondsForControl:
    (const xe::hid::touch::IOSTouchControlDefinition&)control {
  return std::clamp(control.secondary_behavior.hold_seconds, 0.12f, 0.60f);
}

- (void)triggerSecondaryBehaviorPulseForControlIndex:(NSUInteger)control_index
                                              atTime:(CFTimeInterval)current_time {
  if (!runtime_model_ || control_index >= recent_secondary_press_times_.size()) {
    return;
  }

  const auto& controls = runtime_model_->layout().controls;
  if (control_index >= controls.size() ||
      controls[control_index].secondary_behavior.action == xe::hid::touch::IOSTouchAction::kNone) {
    return;
  }

  recent_secondary_press_times_[control_index] = current_time;
}

- (BOOL)hasPendingDoubleTapCandidateForControlIndex:(NSUInteger)control_index
                                             atTime:(CFTimeInterval)current_time {
  if (!runtime_model_ || control_index >= recent_secondary_candidate_times_.size()) {
    return NO;
  }

  const auto& controls = runtime_model_->layout().controls;
  if (control_index >= controls.size()) {
    return NO;
  }

  const CFTimeInterval candidate_time = recent_secondary_candidate_times_[control_index];
  if (candidate_time <= 0.0) {
    return NO;
  }

  return (current_time - candidate_time) <=
         [self doubleTapWindowSecondsForControl:controls[control_index]];
}

- (BOOL)consumeDoubleTapCandidateForControlIndex:(NSUInteger)control_index
                                          atTime:(CFTimeInterval)current_time {
  if (![self hasPendingDoubleTapCandidateForControlIndex:control_index atTime:current_time]) {
    return NO;
  }

  recent_secondary_candidate_times_[control_index] = 0.0;
  [self triggerSecondaryBehaviorPulseForControlIndex:control_index atTime:current_time];
  return YES;
}

- (void)storeDoubleTapCandidateForControlIndex:(NSUInteger)control_index
                                        atTime:(CFTimeInterval)current_time {
  if (control_index >= recent_secondary_candidate_times_.size()) {
    return;
  }
  recent_secondary_candidate_times_[control_index] = current_time;
}

- (void)setSelectedControlAction:(xe::hid::touch::IOSTouchAction)action {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  [self clearEditSnapGuides];
  auto& controls = runtime_model_->mutable_layout().controls;
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= controls.size()) {
    return;
  }

  auto& control = controls[selected_control_index_];
  if (control.action == action) {
    return;
  }
  if (!xe::hid::touch::IsSupportedIOSTouchPrimaryAction(control.type, action)) {
    return;
  }

  [self beginEditLayoutChangeIfNeeded];
  if (control.type == xe::hid::touch::IOSTouchControlType::kActionButton) {
    // Action buttons need their mapped XInput button bits / triggers /
    // hold-while-captured semantics reset and re-derived from the new action.
    xe::hid::touch::ConfigureIOSTouchControlAction(action, &control);
  } else {
    // Move / Look / Pause types don't carry button-mapping data; just swap
    // the action so the publish path routes the control's input to the new
    // thumbstick (or pause behaviour) without disturbing the rest of the
    // control definition.
    control.action = action;
  }
  [self syncControlViewDefinitions];
  [self refreshEditChromeSelection];
  [self refreshEditPreview];
  [self applyCaptureVisualState];
  [self publishResolvedState];
  [self finishEditLayoutChangeIfNeeded];
}

- (void)setSelectedControlLabelHidden:(BOOL)hidden {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  [self clearEditSnapGuides];
  auto& controls = runtime_model_->mutable_layout().controls;
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= controls.size()) {
    return;
  }

  auto& control = controls[selected_control_index_];
  if (control.label_hidden == hidden) {
    return;
  }

  [self beginEditLayoutChangeIfNeeded];
  control.label_hidden = hidden;
  [self syncControlViewDefinitions];
  [self refreshEditChromeSelection];
  [self applyCaptureVisualState];
  [self finishEditLayoutChangeIfNeeded];
}

- (void)resetSelectedControlLabel {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  [self clearEditSnapGuides];
  auto& controls = runtime_model_->mutable_layout().controls;
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= controls.size()) {
    return;
  }

  auto& control = controls[selected_control_index_];
  if (!xe::hid::touch::IOSTouchControlHasCustomLabel(control)) {
    return;
  }

  [self beginEditLayoutChangeIfNeeded];
  xe::hid::touch::ResetIOSTouchControlLabel(&control);
  [self syncControlViewDefinitions];
  [self refreshEditChromeSelection];
  [self applyCaptureVisualState];
  [self finishEditLayoutChangeIfNeeded];
}

- (void)setSelectedControlCustomLabelText:(NSString*)label_text {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  [self clearEditSnapGuides];
  auto& controls = runtime_model_->mutable_layout().controls;
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= controls.size()) {
    return;
  }

  auto& control = controls[selected_control_index_];
  std::string next_label = label_text ? std::string(label_text.UTF8String) : std::string();
  const std::string prior_label = xe::hid::touch::IOSTouchConfiguredControlLabel(control);
  const bool prior_has_custom_label = xe::hid::touch::IOSTouchControlHasCustomLabel(control);
  [self beginEditLayoutChangeIfNeeded];
  xe::hid::touch::SetIOSTouchControlCustomLabel(std::move(next_label), &control);
  if (xe::hid::touch::IOSTouchConfiguredControlLabel(control) == prior_label &&
      xe::hid::touch::IOSTouchControlHasCustomLabel(control) == prior_has_custom_label) {
    edit_history_.CancelChange();
    return;
  }

  [self syncControlViewDefinitions];
  [self refreshEditChromeSelection];
  [self applyCaptureVisualState];
  [self finishEditLayoutChangeIfNeeded];
}

- (UIViewController*)labelEditPresenter {
  UIResponder* responder = self;
  while ((responder = responder.nextResponder)) {
    if ([responder isKindOfClass:[UIViewController class]]) {
      UIViewController* controller = (UIViewController*)responder;
      while (controller.presentedViewController &&
             !controller.presentedViewController.isBeingDismissed) {
        controller = controller.presentedViewController;
      }
      return controller;
    }
  }

  UIViewController* controller = self.window.rootViewController;
  while (controller.presentedViewController &&
         !controller.presentedViewController.isBeingDismissed) {
    controller = controller.presentedViewController;
  }
  return controller;
}

- (void)presentLabelRenameAlert {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  const auto& controls = runtime_model_->layout().controls;
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= controls.size()) {
    return;
  }

  UIViewController* presenter = [self labelEditPresenter];
  if (!presenter) {
    return;
  }

  const auto& control = controls[selected_control_index_];
  NSString* current_label = XeniaTouchConfiguredControlLabelText(control, NO);
  UIAlertController* alert =
      [UIAlertController alertControllerWithTitle:@"Control Label"
                                          message:@"Leave blank to use the default label."
                                   preferredStyle:UIAlertControllerStyleAlert];
  [alert addTextFieldWithConfigurationHandler:^(UITextField* text_field) {
    text_field.text = current_label;
    text_field.placeholder = @"Label";
    text_field.clearButtonMode = UITextFieldViewModeWhileEditing;
    text_field.returnKeyType = UIReturnKeyDone;
  }];

  __unsafe_unretained XeniaTouchControlsOverlayView* unsafe_self = self;
  __unsafe_unretained UIAlertController* unsafe_alert = alert;
  [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                            style:UIAlertActionStyleCancel
                                          handler:nil]];
  UIAlertAction* save_action =
      [UIAlertAction actionWithTitle:@"Save"
                               style:UIAlertActionStyleDefault
                             handler:^(__unused UIAlertAction* action_handler) {
                               UITextField* text_field = unsafe_alert.textFields.firstObject;
                               [unsafe_self setSelectedControlCustomLabelText:text_field.text];
                             }];
  [alert addAction:save_action];
  alert.preferredAction = save_action;
  [presenter presentViewController:alert animated:YES completion:nil];
}

- (void)setSelectedControlBehaviorTrigger:(xe::hid::touch::IOSTouchInteractionTrigger)trigger {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  [self clearEditSnapGuides];
  auto& controls = runtime_model_->mutable_layout().controls;
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= controls.size()) {
    return;
  }

  auto& control = controls[selected_control_index_];
  const bool supports_behavior =
      control.type == xe::hid::touch::IOSTouchControlType::kActionButton ||
      control.type == xe::hid::touch::IOSTouchControlType::kMoveStick;
  if (!supports_behavior || control.secondary_behavior.trigger == trigger) {
    return;
  }

  [self beginEditLayoutChangeIfNeeded];
  control.secondary_behavior.trigger = trigger;
  control.secondary_behavior.hold_seconds =
      xe::hid::touch::DefaultIOSTouchHoldSecondsForInteractionTrigger(trigger);
  if (trigger == xe::hid::touch::IOSTouchInteractionTrigger::kNone) {
    control.secondary_behavior.enables_relative_look = false;
  } else if (trigger == xe::hid::touch::IOSTouchInteractionTrigger::kHoldDrag) {
    control.secondary_behavior.enables_relative_look = true;
    if (control.secondary_behavior.relative_look_scale <= 0.0f) {
      control.secondary_behavior.relative_look_scale = 1.0f;
    }
  } else {
    control.secondary_behavior.enables_relative_look = false;
  }
  if (control.type == xe::hid::touch::IOSTouchControlType::kMoveStick &&
      trigger == xe::hid::touch::IOSTouchInteractionTrigger::kDoubleTapForward &&
      control.secondary_behavior.action == xe::hid::touch::IOSTouchAction::kNone) {
    control.secondary_behavior.action = xe::hid::touch::IOSTouchAction::kLeftThumb;
  }
  [self syncControlViewDefinitions];
  [self refreshEditChromeSelection];
  [self refreshEditPreview];
  [self applyCaptureVisualState];
  [self publishResolvedState];
  [self finishEditLayoutChangeIfNeeded];
}

- (void)setSelectedControlBehaviorAction:(xe::hid::touch::IOSTouchAction)action {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  [self clearEditSnapGuides];
  auto& controls = runtime_model_->mutable_layout().controls;
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= controls.size()) {
    return;
  }

  auto& control = controls[selected_control_index_];
  const bool supports_behavior =
      control.type == xe::hid::touch::IOSTouchControlType::kActionButton ||
      control.type == xe::hid::touch::IOSTouchControlType::kMoveStick;
  if (!supports_behavior || control.secondary_behavior.action == action) {
    return;
  }

  [self beginEditLayoutChangeIfNeeded];
  control.secondary_behavior.action = action;
  [self syncControlViewDefinitions];
  [self refreshEditChromeSelection];
  [self refreshEditPreview];
  [self applyCaptureVisualState];
  [self publishResolvedState];
  [self finishEditLayoutChangeIfNeeded];
}

- (void)setSelectedControlTintStyle:(xe::hid::touch::IOSTouchTintStyle)tint_style {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  [self clearEditSnapGuides];
  auto& controls = runtime_model_->mutable_layout().controls;
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= controls.size()) {
    return;
  }

  auto& control = controls[selected_control_index_];
  if (control.tint_style == tint_style) {
    return;
  }

  [self beginEditLayoutChangeIfNeeded];
  control.tint_style = tint_style;
  [self syncControlViewDefinitions];
  [self refreshEditChromeSelection];
  [self refreshEditPreview];
  [self applyCaptureVisualState];
  [self finishEditLayoutChangeIfNeeded];
}

- (void)setSelectedControlShape:(xe::hid::touch::IOSTouchControlShape)shape {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  [self clearEditSnapGuides];
  auto& controls = runtime_model_->mutable_layout().controls;
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= controls.size()) {
    return;
  }

  auto& control = controls[selected_control_index_];
  if (control.type != xe::hid::touch::IOSTouchControlType::kActionButton ||
      control.shape == shape) {
    return;
  }

  [self beginEditLayoutChangeIfNeeded];
  control.shape = shape;
  [self syncControlViewDefinitions];
  [self refreshEditChromeSelection];
  [self setNeedsLayout];
  [self layoutIfNeeded];
  [self applyCaptureVisualState];
  [self publishResolvedState];
  [self finishEditLayoutChangeIfNeeded];
}

- (BOOL)selectedControlSupportsLookScaleTuning {
  if (!runtime_model_ || selected_control_index_ == NSNotFound) {
    return NO;
  }
  const auto& controls = runtime_model_->layout().controls;
  if (selected_control_index_ >= controls.size()) {
    return NO;
  }
  const auto& control = controls[selected_control_index_];
  return control.type == xe::hid::touch::IOSTouchControlType::kLookSwipeZone ||
         control.enables_relative_look;
}

- (float)selectedControlLookScale {
  if (!runtime_model_ || selected_control_index_ == NSNotFound) {
    return 1.0f;
  }
  const auto& controls = runtime_model_->layout().controls;
  if (selected_control_index_ >= controls.size()) {
    return 1.0f;
  }
  return std::clamp(controls[selected_control_index_].relative_look_scale, 0.25f, 4.0f);
}

- (void)setSelectedControlLookScale:(float)look_scale {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  [self clearEditSnapGuides];
  auto& controls = runtime_model_->mutable_layout().controls;
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= controls.size()) {
    return;
  }

  auto& control = controls[selected_control_index_];
  if (!(control.type == xe::hid::touch::IOSTouchControlType::kLookSwipeZone ||
        control.enables_relative_look)) {
    return;
  }

  const float clamped_scale = std::clamp(look_scale, 0.25f, 4.0f);
  if (std::abs(control.relative_look_scale - clamped_scale) < 0.001f) {
    return;
  }

  [self beginEditLayoutChangeIfNeeded];
  control.relative_look_scale = clamped_scale;
  [self syncControlViewDefinitions];
  [self refreshEditChromeSelection];
  [self refreshEditPreview];
  [self applyCaptureVisualState];
  [self publishResolvedState];
  [self finishEditLayoutChangeIfNeeded];
}

- (BOOL)layoutContainsControlType:(xe::hid::touch::IOSTouchControlType)type {
  if (!runtime_model_) {
    return NO;
  }
  const auto& controls = runtime_model_->layout().controls;
  return std::any_of(controls.begin(), controls.end(),
                     [type](const xe::hid::touch::IOSTouchControlDefinition& control) {
                       return control.type == type;
                     });
}

- (void)addControlOfType:(xe::hid::touch::IOSTouchControlType)type {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  if (runtime_model_->layout().controls.size() >= xe::hid::touch::kMaxIOSTouchControls) {
    return;
  }
  if (type == xe::hid::touch::IOSTouchControlType::kActionButton) {
    [self addNewActionButton];
    return;
  }
  if ([self layoutContainsControlType:type]) {
    return;
  }

  [self clearEditSnapGuides];
  [self beginEditLayoutChangeIfNeeded];
  auto& layout = runtime_model_->mutable_layout();
  xe::hid::touch::IOSTouchControlDefinition control =
      xe::hid::touch::CreateDefaultIOSTouchControlDefinition(type);
  const std::string selected_identifier = control.identifier;
  layout.controls.push_back(std::move(control));
  [self refreshLayoutModel];
  [self selectControlWithIdentifier:selected_identifier];
  [self publishResolvedState];
  [self finishEditLayoutChangeIfNeeded];
}

- (NSUInteger)nearestSizeMatchControlIndexForSelectedControl {
  if (!runtime_model_ || selected_control_index_ == NSNotFound ||
      selected_control_index_ >= resolved_control_frames_.size()) {
    return NSNotFound;
  }

  const auto& controls = runtime_model_->layout().controls;
  if (selected_control_index_ >= controls.size()) {
    return NSNotFound;
  }

  const auto& selected_control = controls[selected_control_index_];
  if (selected_control.type != xe::hid::touch::IOSTouchControlType::kActionButton) {
    return NSNotFound;
  }

  const xe::hid::touch::IOSTouchRect& selected_frame =
      resolved_control_frames_[selected_control_index_];
  if (selected_frame.width <= 0.0f || selected_frame.height <= 0.0f) {
    return NSNotFound;
  }
  const CGFloat selected_center_x = selected_frame.x + selected_frame.width * 0.5f;
  const CGFloat selected_center_y = selected_frame.y + selected_frame.height * 0.5f;

  auto find_best_match = [&](BOOL same_type_only) {
    NSUInteger best_index = NSNotFound;
    CGFloat best_distance = CGFLOAT_MAX;
    for (NSUInteger control_index = 0;
         control_index < MIN(static_cast<NSUInteger>(controls.size()),
                             static_cast<NSUInteger>(resolved_control_frames_.size()));
         ++control_index) {
      if (control_index == selected_control_index_) {
        continue;
      }
      const auto& candidate = controls[control_index];
      if (candidate.type == xe::hid::touch::IOSTouchControlType::kLookSwipeZone) {
        continue;
      }
      if (same_type_only && candidate.type != selected_control.type) {
        continue;
      }
      const xe::hid::touch::IOSTouchRect& candidate_frame = resolved_control_frames_[control_index];
      const CGFloat candidate_center_x = candidate_frame.x + candidate_frame.width * 0.5f;
      const CGFloat candidate_center_y = candidate_frame.y + candidate_frame.height * 0.5f;
      const CGFloat distance = std::hypot(candidate_center_x - selected_center_x,
                                          candidate_center_y - selected_center_y);
      if (distance < best_distance) {
        best_distance = distance;
        best_index = control_index;
      }
    }
    return best_index;
  };

  NSUInteger match_index = find_best_match(YES);
  return match_index;
}

- (void)matchSelectedControlSizeToNearestSibling {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  NSUInteger match_index = [self nearestSizeMatchControlIndexForSelectedControl];
  if (match_index == NSNotFound || selected_control_index_ == NSNotFound ||
      selected_control_index_ >= resolved_control_frames_.size() ||
      match_index >= resolved_control_frames_.size()) {
    return;
  }

  [self clearEditSnapGuides];
  auto& controls = runtime_model_->mutable_layout().controls;
  if (selected_control_index_ >= controls.size() || match_index >= controls.size()) {
    return;
  }

  const xe::hid::touch::IOSTouchRect& selected_frame =
      resolved_control_frames_[selected_control_index_];
  const xe::hid::touch::IOSTouchRect& match_frame = resolved_control_frames_[match_index];
  xe::hid::touch::IOSTouchRect candidate = {
      selected_frame.x + (selected_frame.width - match_frame.width) * 0.5f,
      selected_frame.y + (selected_frame.height - match_frame.height) * 0.5f,
      match_frame.width,
      match_frame.height,
  };
  const bool match_is_portrait = TouchOverlayIsPortraitForView(self);
  xe::hid::touch::IOSTouchControlDefinition& match_control = controls[selected_control_index_];
  xe::hid::touch::IOSTouchLayoutSpace position_space = TouchSafeAreaSpaceForView(self);
  xe::hid::touch::IOSTouchLayoutSpace size_space =
      TouchControlSizeSpaceForControlType(self, match_control.type);
  if (position_space.IsEmpty() || size_space.IsEmpty()) {
    return;
  }
  [self beginEditLayoutChangeIfNeeded];
  xe::hid::touch::IOSTouchRect& match_active_frame =
      xe::hid::touch::MutableActiveControlFrameForOrientation(match_control, match_is_portrait);
  match_active_frame = NormalizedControlFrameFromResolvedFrame(candidate, position_space,
                                                               size_space, match_control.type);
  const xe::hid::touch::IOSTouchRect committed_match_frame = match_active_frame;

  for (TouchCaptureState& capture : active_captures_) {
    if (capture.control_index == selected_control_index_) {
      capture.normalized_frame_at_capture = committed_match_frame;
      capture.anchor_point = capture.current_point;
    }
  }

  [self syncControlViewDefinitions];
  [self refreshEditChromeSelection];
  [self setNeedsLayout];
  [self layoutIfNeeded];
  [self applyCaptureVisualState];
  [self finishEditLayoutChangeIfNeeded];
}

- (CGRect)preferredEditChromeFrameForSafeArea:(const xe::hid::touch::IOSTouchLayoutSpace&)safe_area
                                        width:(CGFloat)chrome_width
                                       height:(CGFloat)chrome_height {
  const CGFloat chrome_margin = 14.0f;
  const auto candidates =
      EditChromeDockCandidateFrames(safe_area, chrome_margin, chrome_width, chrome_height);

  if (!runtime_model_) {
    return candidates[0];
  }

  const auto& controls = runtime_model_->layout().controls;
  CGFloat best_penalty = CGFLOAT_MAX;
  CGRect best_frame = candidates[0];
  for (CGRect candidate : candidates) {
    CGFloat penalty = 0.0f;
    for (NSUInteger control_index = 0;
         control_index < MIN(static_cast<NSUInteger>(controls.size()),
                             static_cast<NSUInteger>(resolved_control_frames_.size()));
         ++control_index) {
      if (controls[control_index].type == xe::hid::touch::IOSTouchControlType::kLookSwipeZone) {
        continue;
      }
      CGRect control_frame = CGRectFromTouchRect(resolved_control_frames_[control_index]);
      CGRect intersection = CGRectIntersection(candidate, control_frame);
      if (CGRectIsNull(intersection) || CGRectIsEmpty(intersection)) {
        continue;
      }
      CGFloat weight = 1.0f;
      if (control_index == selected_control_index_) {
        weight = 5.0f;
      } else if (control_index == pause_control_index_) {
        weight = 3.0f;
      }
      penalty += CGRectGetWidth(intersection) * CGRectGetHeight(intersection) * weight;
    }
    if (penalty < best_penalty) {
      best_penalty = penalty;
      best_frame = candidate;
    }
  }
  return best_frame;
}

- (CGRect)editChromeFrameForDockIndex:(NSInteger)dock_index
                             safeArea:(const xe::hid::touch::IOSTouchLayoutSpace&)safe_area
                                width:(CGFloat)chrome_width
                               height:(CGFloat)chrome_height {
  const auto candidates =
      EditChromeDockCandidateFrames(safe_area, 14.0f, chrome_width, chrome_height);
  if (dock_index < 0 || dock_index >= static_cast<NSInteger>(candidates.size())) {
    return [self preferredEditChromeFrameForSafeArea:safe_area
                                               width:chrome_width
                                              height:chrome_height];
  }
  return candidates[static_cast<size_t>(dock_index)];
}

- (CGRect)clampedEditChromeFrame:(CGRect)frame
                        safeArea:(const xe::hid::touch::IOSTouchLayoutSpace&)safe_area {
  const CGFloat chrome_margin = 14.0f;
  const CGFloat min_x = safe_area.origin_x + chrome_margin;
  const CGFloat max_x =
      MAX(min_x, safe_area.origin_x + safe_area.width - CGRectGetWidth(frame) - chrome_margin);
  const CGFloat min_y = safe_area.origin_y + chrome_margin;
  const CGFloat max_y =
      MAX(min_y, safe_area.origin_y + safe_area.height - CGRectGetHeight(frame) - chrome_margin);
  frame.origin.x = std::clamp(CGRectGetMinX(frame), min_x, max_x);
  frame.origin.y = std::clamp(CGRectGetMinY(frame), min_y, max_y);
  return CGRectIntegral(frame);
}

- (NSInteger)nearestEditChromeDockIndexForFrame:(CGRect)frame
                                       safeArea:
                                           (const xe::hid::touch::IOSTouchLayoutSpace&)safe_area
                                          width:(CGFloat)chrome_width
                                         height:(CGFloat)chrome_height {
  const auto candidates =
      EditChromeDockCandidateFrames(safe_area, 14.0f, chrome_width, chrome_height);
  CGPoint frame_center = CGPointMake(CGRectGetMidX(frame), CGRectGetMidY(frame));
  NSInteger best_index = 0;
  CGFloat best_distance = CGFLOAT_MAX;
  for (NSInteger candidate_index = 0; candidate_index < static_cast<NSInteger>(candidates.size());
       ++candidate_index) {
    CGPoint candidate_center =
        CGPointMake(CGRectGetMidX(candidates[static_cast<size_t>(candidate_index)]),
                    CGRectGetMidY(candidates[static_cast<size_t>(candidate_index)]));
    const CGFloat distance =
        std::hypot(frame_center.x - candidate_center.x, frame_center.y - candidate_center.y);
    if (distance < best_distance) {
      best_distance = distance;
      best_index = candidate_index;
    }
  }
  return best_index;
}

- (CGRect)resolvedEditChromeFrameForSafeArea:(const xe::hid::touch::IOSTouchLayoutSpace&)safe_area
                                       width:(CGFloat)chrome_width
                                      height:(CGFloat)chrome_height {
  if (edit_chrome_drag_active_) {
    return [self clampedEditChromeFrame:edit_chrome_drag_frame_ safeArea:safe_area];
  }
  if (edit_chrome_dock_index_ != kEditChromeDockAuto) {
    return [self editChromeFrameForDockIndex:edit_chrome_dock_index_
                                    safeArea:safe_area
                                       width:chrome_width
                                      height:chrome_height];
  }
  return [self preferredEditChromeFrameForSafeArea:safe_area
                                             width:chrome_width
                                            height:chrome_height];
}

- (CGRect)editChromeHeaderDragFrame {
  if (edit_chrome_.hidden) {
    return CGRectZero;
  }
  return CGRectMake(CGRectGetMinX(edit_chrome_.frame), CGRectGetMinY(edit_chrome_.frame),
                    CGRectGetWidth(edit_chrome_.frame),
                    MIN(kEditChromeHeaderHeight, CGRectGetHeight(edit_chrome_.frame)));
}

- (void)clearEditChromeDragState {
  edit_chrome_drag_active_ = NO;
  edit_chrome_drag_touch_ = nil;
  edit_chrome_drag_touch_offset_ = CGPointZero;
}

- (void)clearEditSnapGuides {
  active_snap_vertical_guides_.clear();
  active_snap_horizontal_guides_.clear();
  [self updateEditSnapGuidesPath];
}

- (void)updateEditSnapGuidesPath {
  xe::hid::touch::IOSTouchLayoutSpace safe_area = TouchSafeAreaSpaceForView(self);
  const bool guides_empty =
      !editing_controls_enabled_ || safe_area.IsEmpty() ||
      (active_snap_vertical_guides_.empty() && active_snap_horizontal_guides_.empty());

  // Keep the overlay/layer non-hidden while in edit mode and animate opacity
  // instead of toggling hidden — this lets the guides fade in/out smoothly as
  // the user drags a control past alignment positions, rather than flashing
  // in/out instantly.
  edit_snap_guides_overlay_.hidden = !editing_controls_enabled_;
  edit_snap_guides_layer_.hidden = !editing_controls_enabled_;

  if (!guides_empty) {
    UIBezierPath* guide_path = [UIBezierPath bezierPath];
    for (CGFloat x : active_snap_vertical_guides_) {
      [guide_path moveToPoint:CGPointMake(x, safe_area.origin_y)];
      [guide_path addLineToPoint:CGPointMake(x, safe_area.origin_y + safe_area.height)];
    }
    for (CGFloat y : active_snap_horizontal_guides_) {
      [guide_path moveToPoint:CGPointMake(safe_area.origin_x, y)];
      [guide_path addLineToPoint:CGPointMake(safe_area.origin_x + safe_area.width, y)];
    }
    edit_snap_guides_layer_.path = guide_path.CGPath;
  }

  [CATransaction begin];
  [CATransaction setAnimationDuration:0.10];
  [CATransaction setAnimationTimingFunction:[CAMediaTimingFunction
                                                functionWithName:kCAMediaTimingFunctionEaseOut]];
  edit_snap_guides_layer_.opacity = guides_empty ? 0.0f : 1.0f;
  [CATransaction commit];

  // Rising-edge snap haptic: only fire when guides become visible (a fresh
  // alignment engagement), not on every drag tick that keeps them present.
  const BOOL guides_visible_now = !guides_empty;
  if (guides_visible_now && !snap_guides_were_visible_) {
    [self playSnapHaptic];
  }
  snap_guides_were_visible_ = guides_visible_now;
}

- (void)clearEditPinchState {
  edit_pinch_active_ = NO;
  edit_pinch_control_index_ = NSNotFound;
  edit_pinch_touch_a_ = nil;
  edit_pinch_touch_b_ = nil;
  edit_pinch_initial_distance_ = 0.0f;
  edit_pinch_initial_frame_ = {};
}

- (void)clearLookMotionState {
  std::fill(recent_look_vectors_.begin(), recent_look_vectors_.end(), CGPointZero);
  std::fill(recent_look_motion_times_.begin(), recent_look_motion_times_.end(), 0.0);
}

- (void)clearLookMotionStateForControlIndex:(NSUInteger)control_index {
  if (control_index >= recent_look_vectors_.size() ||
      control_index >= recent_look_motion_times_.size()) {
    return;
  }
  recent_look_vectors_[control_index] = CGPointZero;
  recent_look_motion_times_[control_index] = 0.0;
}

- (void)storeLookMotion:(CGPoint)look_vector
        forControlIndex:(NSUInteger)control_index
                 atTime:(CFTimeInterval)current_time {
  if (control_index >= recent_look_vectors_.size() ||
      control_index >= recent_look_motion_times_.size()) {
    return;
  }
  recent_look_vectors_[control_index] = look_vector;
  recent_look_motion_times_[control_index] = current_time;
}

- (xe::hid::touch::IOSTouchRect)
    snappedResolvedFrameForControlIndex:(NSUInteger)control_index
                         candidateFrame:(const xe::hid::touch::IOSTouchRect&)candidate_frame
                               safeArea:(const xe::hid::touch::IOSTouchLayoutSpace&)safe_area
                            gestureMode:(TouchCaptureState::EditGestureMode)gesture_mode
                    preserveAspectRatio:(BOOL)preserve_aspect_ratio
                         preserveCenter:(BOOL)preserve_center {
  const auto& controls = runtime_model_->layout().controls;
  if (control_index >= controls.size()) {
    return candidate_frame;
  }
  const auto& control = controls[control_index];
  xe::hid::touch::IOSTouchLayoutSpace size_space =
      TouchControlSizeSpaceForControlType(self, control.type);
  if (safe_area.IsEmpty() || size_space.IsEmpty()) {
    return candidate_frame;
  }

  TouchEditSnapOptions options;
  options.grid_enabled = edit_grid_enabled_;
  options.grid_spacing = kEditGridSpacingPoints;
  options.move_snap_threshold =
      edit_grid_enabled_ ? kEditGridMoveSnapThresholdPoints : kEditMoveSnapThresholdPoints;
  options.resize_snap_threshold =
      edit_grid_enabled_ ? kEditGridResizeSnapThresholdPoints : kEditResizeSnapThresholdPoints;
  options.canonical_control_sizes = kEditCanonicalControlSizes;
  options.canonical_control_size_count =
      sizeof(kEditCanonicalControlSizes) / sizeof(kEditCanonicalControlSizes[0]);
  TouchEditSnapResult result = SnapTouchEditResolvedFrame(
      control_index, controls, resolved_control_frames_, candidate_frame, safe_area, size_space,
      gesture_mode == TouchCaptureState::EditGestureMode::kMove ? TouchEditGestureMode::kMove
                                                                : TouchEditGestureMode::kResize,
      preserve_aspect_ratio, preserve_center, options);
  active_snap_vertical_guides_ = std::move(result.vertical_guides);
  active_snap_horizontal_guides_ = std::move(result.horizontal_guides);
  return result.frame;
}

- (BOOL)tryBeginEditPinchWithTouch:(UITouch*)touch atPoint:(CGPoint)point {
  if (!editing_controls_enabled_ || !runtime_model_ || edit_pinch_active_ ||
      selected_control_index_ == NSNotFound ||
      selected_control_index_ >= runtime_model_->layout().controls.size() ||
      selected_control_index_ >= resolved_control_frames_.size()) {
    return NO;
  }

  const auto& controls = runtime_model_->layout().controls;
  const auto& control = controls[selected_control_index_];
  if (!TouchControlContainsPoint(control, resolved_control_frames_[selected_control_index_],
                                 point)) {
    return NO;
  }

  const NSUInteger selected_control_index = selected_control_index_;
  auto primary_capture_it =
      std::find_if(active_captures_.begin(), active_captures_.end(),
                   [selected_control_index](const TouchCaptureState& capture) {
                     return capture.control_index == selected_control_index &&
                            capture.edit_gesture_mode == TouchCaptureState::EditGestureMode::kMove;
                   });
  if (primary_capture_it == active_captures_.end()) {
    return NO;
  }

  CGPoint primary_point = [primary_capture_it->touch locationInView:self];
  const CGFloat initial_distance = std::hypot(primary_point.x - point.x, primary_point.y - point.y);
  if (initial_distance < 18.0f) {
    return NO;
  }

  [self beginEditLayoutChangeIfNeeded];
  edit_pinch_active_ = YES;
  edit_pinch_control_index_ = selected_control_index_;
  edit_pinch_touch_a_ = primary_capture_it->touch;
  edit_pinch_touch_b_ = touch;
  edit_pinch_initial_distance_ = initial_distance;
  // Pinch always operates on the orientation currently being edited; the
  // commit path (updatePinchedControlFrame) writes back through the same
  // orientation choke point.
  const bool pinch_begin_is_portrait = TouchOverlayIsPortraitForView(self);
  edit_pinch_initial_frame_ = xe::hid::touch::ActiveControlFrameForOrientation(
      controls[selected_control_index_], pinch_begin_is_portrait);
  primary_capture_it->current_point = primary_point;
  primary_capture_it->normalized_frame_at_capture = edit_pinch_initial_frame_;
  [self clearEditSnapGuides];
  return YES;
}

- (void)endEditPinchRetainingTouch:(UITouch*)remaining_touch {
  const NSUInteger pinch_control_index = edit_pinch_control_index_;
  xe::hid::touch::IOSTouchRect current_frame = {};
  if (runtime_model_ && pinch_control_index != NSNotFound &&
      pinch_control_index < runtime_model_->layout().controls.size()) {
    const bool pinch_end_is_portrait = TouchOverlayIsPortraitForView(self);
    current_frame = xe::hid::touch::ActiveControlFrameForOrientation(
        runtime_model_->layout().controls[pinch_control_index], pinch_end_is_portrait);
  }

  [self clearEditPinchState];
  [self clearEditSnapGuides];

  if (!remaining_touch || !runtime_model_ || pinch_control_index == NSNotFound ||
      pinch_control_index >= runtime_model_->layout().controls.size()) {
    return;
  }

  CGPoint remaining_point = [remaining_touch locationInView:self];
  auto capture_it = std::find_if(active_captures_.begin(), active_captures_.end(),
                                 [remaining_touch](const TouchCaptureState& capture) {
                                   return capture.touch == remaining_touch;
                                 });
  if (capture_it != active_captures_.end()) {
    capture_it->control_index = pinch_control_index;
    capture_it->anchor_point = remaining_point;
    capture_it->current_point = remaining_point;
    capture_it->began_time = CACurrentMediaTime();
    capture_it->normalized_frame_at_capture = current_frame;
    capture_it->edit_gesture_mode = TouchCaptureState::EditGestureMode::kMove;
    return;
  }

  TouchCaptureState capture;
  capture.touch = remaining_touch;
  capture.control_index = pinch_control_index;
  capture.anchor_point = remaining_point;
  capture.current_point = remaining_point;
  capture.began_time = CACurrentMediaTime();
  capture.normalized_frame_at_capture = current_frame;
  capture.edit_gesture_mode = TouchCaptureState::EditGestureMode::kMove;
  active_captures_.push_back(capture);
}

- (void)updatePinchedControlFrame {
  if (!editing_controls_enabled_ || !runtime_model_ || !edit_pinch_active_ ||
      edit_pinch_control_index_ == NSNotFound ||
      edit_pinch_control_index_ >= runtime_model_->layout().controls.size()) {
    return;
  }

  xe::hid::touch::IOSTouchControlDefinition& pinch_control =
      runtime_model_->mutable_layout().controls[edit_pinch_control_index_];
  xe::hid::touch::IOSTouchLayoutSpace position_space = TouchSafeAreaSpaceForView(self);
  xe::hid::touch::IOSTouchLayoutSpace size_space =
      TouchControlSizeSpaceForControlType(self, pinch_control.type);
  if (position_space.IsEmpty() || size_space.IsEmpty() || edit_pinch_initial_distance_ <= 0.0f) {
    return;
  }

  CGPoint point_a = [edit_pinch_touch_a_ locationInView:self];
  CGPoint point_b = [edit_pinch_touch_b_ locationInView:self];
  const CGFloat current_distance = std::hypot(point_a.x - point_b.x, point_a.y - point_b.y);
  const float scale =
      std::clamp(static_cast<float>(current_distance / edit_pinch_initial_distance_), 0.45f, 2.75f);
  xe::hid::touch::IOSTouchRect initial_resolved = ResolveNormalizedControlFrame(
      edit_pinch_initial_frame_, position_space, size_space, pinch_control.type);
  const float center_x = initial_resolved.x + initial_resolved.width * 0.5f;
  const float center_y = initial_resolved.y + initial_resolved.height * 0.5f;
  xe::hid::touch::IOSTouchRect candidate = initial_resolved;
  candidate.width *= scale;
  candidate.height *= scale;
  candidate.x = center_x - candidate.width * 0.5f;
  candidate.y = center_y - candidate.height * 0.5f;

  xe::hid::touch::IOSTouchRect snapped =
      [self snappedResolvedFrameForControlIndex:edit_pinch_control_index_
                                 candidateFrame:candidate
                                       safeArea:position_space
                                    gestureMode:TouchCaptureState::EditGestureMode::kResize
                            preserveAspectRatio:YES
                                 preserveCenter:YES];
  [self beginEditLayoutChangeIfNeeded];
  const bool pinch_commit_is_portrait = TouchOverlayIsPortraitForView(self);
  xe::hid::touch::IOSTouchRect& pinch_active_frame =
      xe::hid::touch::MutableActiveControlFrameForOrientation(pinch_control,
                                                              pinch_commit_is_portrait);
  pinch_active_frame = NormalizedControlFrameFromResolvedFrame(snapped, position_space, size_space,
                                                               pinch_control.type);
  const xe::hid::touch::IOSTouchRect committed_pinch_frame = pinch_active_frame;

  UITouch* pinch_touch_a = edit_pinch_touch_a_;
  auto capture_it = std::find_if(
      active_captures_.begin(), active_captures_.end(),
      [pinch_touch_a](const TouchCaptureState& capture) { return capture.touch == pinch_touch_a; });
  if (capture_it != active_captures_.end()) {
    capture_it->normalized_frame_at_capture = committed_pinch_frame;
    capture_it->current_point = point_a;
  }
}

- (void)updateConflictHighlights {
  const NSUInteger control_count =
      MIN(control_views_.count, static_cast<NSUInteger>(resolved_control_frames_.size()));
  conflicting_control_indices_.assign(control_count, false);

  if (!runtime_model_ || !editing_controls_enabled_) {
    for (NSUInteger control_index = 0; control_index < control_count; ++control_index) {
      [[control_views_ objectAtIndex:control_index] setConflictHighlighted:NO];
    }
    return;
  }

  const auto& controls = runtime_model_->layout().controls;
  for (NSUInteger left_index = 0; left_index < control_count; ++left_index) {
    if (controls[left_index].type == xe::hid::touch::IOSTouchControlType::kLookSwipeZone) {
      continue;
    }
    CGRect left_frame = CGRectFromTouchRect(resolved_control_frames_[left_index]);
    for (NSUInteger right_index = left_index + 1; right_index < control_count; ++right_index) {
      if (controls[right_index].type == xe::hid::touch::IOSTouchControlType::kLookSwipeZone) {
        continue;
      }
      CGRect right_frame = CGRectFromTouchRect(resolved_control_frames_[right_index]);
      if (CGRectIntersectsRect(left_frame, right_frame)) {
        conflicting_control_indices_[left_index] = true;
        conflicting_control_indices_[right_index] = true;
      }
    }
  }

  for (NSUInteger control_index = 0; control_index < control_count; ++control_index) {
    [[control_views_ objectAtIndex:control_index]
        setConflictHighlighted:conflicting_control_indices_[control_index]];
  }
}

- (void)selectControlWithIdentifier:(const std::string&)identifier {
  if (!runtime_model_) {
    return;
  }

  const auto& controls = runtime_model_->layout().controls;
  for (NSUInteger control_index = 0; control_index < controls.size(); ++control_index) {
    if (controls[control_index].identifier == identifier) {
      [self setSelectedControlIndex:control_index];
      return;
    }
  }
}

- (void)setSelectedControlIndex:(NSUInteger)selected_control_index {
  const BOOL changed = selected_control_index_ != selected_control_index;
  selected_control_index_ = selected_control_index;
  [self clearEditSnapGuides];
  [self refreshEditChromeSelection];
  [self refreshEditPreview];
  [self applyCaptureVisualState];
  if (changed && editing_controls_enabled_ && selected_control_index != NSNotFound) {
    [self playSelectionHaptic];
  }
}

- (void)resetInteractionState {
  active_captures_.clear();
  [self clearEditChromeDragState];
  [self clearEditPinchState];
  [self clearEditSnapGuides];
  std::fill(recent_action_press_times_.begin(), recent_action_press_times_.end(), 0.0);
  std::fill(recent_secondary_press_times_.begin(), recent_secondary_press_times_.end(), 0.0);
  std::fill(recent_secondary_candidate_times_.begin(), recent_secondary_candidate_times_.end(),
            0.0);
  [self clearLookMotionState];
  move_knob_.hidden = YES;
  for (XeniaTouchControlShellView* control_view in control_views_) {
    [control_view setTouchActive:NO];
  }
}

- (void)syncControlViewDefinitions {
  if (!runtime_model_) {
    return;
  }

  const auto& controls = runtime_model_->layout().controls;
  const NSUInteger control_count =
      MIN(control_views_.count, static_cast<NSUInteger>(controls.size()));
  for (NSUInteger control_index = 0; control_index < control_count; ++control_index) {
    [[control_views_ objectAtIndex:control_index] applyControlDefinition:controls[control_index]];
  }
  [self refreshEditPreview];
}

- (BOOL)isEditingControlsEnabled {
  return editing_controls_enabled_;
}

- (void)setEditingControlsEnabled:(BOOL)enabled animated:(BOOL)animated {
  if (editing_controls_enabled_ == enabled) {
    return;
  }

  editing_controls_enabled_ = enabled;
  [self resetInteractionState];
  if (enabled) {
    [self resetEditLayoutHistory];
    edit_chrome_minimized_ = NO;
    // Restore the dock corner the user last left the chrome at, so the
    // editor opens in the same place across sessions instead of resetting
    // to the default each time.
    NSInteger persisted_dock =
        [[NSUserDefaults standardUserDefaults] integerForKey:@"XeniaTouchEditChromeDock"];
    edit_chrome_dock_index_ = (persisted_dock >= 0 && persisted_dock < kEditChromeDockCount)
                                  ? persisted_dock
                                  : kEditChromeDockAuto;
    [self clearEditChromeDragState];
    if (selected_control_index_ == NSNotFound && runtime_model_) {
      const auto& controls = runtime_model_->layout().controls;
      if (!controls.empty()) {
        [self
            setSelectedControlIndex:(move_control_index_ != NSNotFound ? move_control_index_ : 0)];
      }
    } else {
      [self refreshEditChromeSelection];
      [self refreshEditPreview];
    }
    edit_grid_overlay_.hidden = !edit_grid_enabled_;
    edit_grid_overlay_.alpha = edit_grid_enabled_ ? 1.0 : 0.0;
    edit_safe_area_guide_.hidden = NO;
    edit_chrome_.hidden = NO;
    edit_snap_guides_overlay_.hidden = YES;
    [self seedEditLayoutHistoryIfNeeded];
  } else {
    [self resetEditLayoutHistory];
    edit_showing_layout_library_ = NO;
    edit_chrome_minimized_ = NO;
    edit_chrome_dock_index_ = kEditChromeDockAuto;
    [self clearEditChromeDragState];
  }
  // First-responder dance so hardware-keyboard `keyCommands` (⌘Z, ⇧⌘Z, ⌘D,
  // ⌘⌫, ⌘M, Esc) are routed to us only while the editor is active.
  if (enabled) {
    [self becomeFirstResponder];
  } else {
    [self resignFirstResponder];
  }
  [self setNeedsLayout];
  [self publishResolvedState];

  if (!animated) {
    edit_chrome_.alpha = enabled ? 1.0 : 0.0;
    edit_safe_area_guide_.alpha = enabled ? 1.0 : 0.0;
    edit_snap_guides_overlay_.alpha = enabled ? 1.0 : 0.0;
    edit_grid_overlay_.hidden = !enabled || !edit_grid_enabled_;
    edit_safe_area_guide_.hidden = !enabled;
    edit_chrome_.hidden = !enabled;
    edit_snap_guides_overlay_.hidden = YES;
    return;
  }

  if (enabled) {
    edit_chrome_.alpha = 0.0;
    edit_safe_area_guide_.alpha = 0.0;
    edit_grid_overlay_.alpha = 0.0;
  }
  [UIView animateWithDuration:0.18
      animations:^{
        edit_chrome_.alpha = enabled ? 1.0 : 0.0;
        edit_safe_area_guide_.alpha = enabled ? 1.0 : 0.0;
        edit_grid_overlay_.alpha = (enabled && edit_grid_enabled_) ? 1.0 : 0.0;
      }
      completion:^(__unused BOOL finished) {
        if (!enabled) {
          edit_snap_guides_overlay_.hidden = YES;
          edit_grid_overlay_.hidden = YES;
          edit_safe_area_guide_.hidden = YES;
          edit_chrome_.hidden = YES;
        }
      }];
}

- (void)adjustSelectedControlSizeByScale:(float)scale {
  if (!editing_controls_enabled_ || !runtime_model_ || scale <= 0.0f) {
    return;
  }

  auto& controls = runtime_model_->mutable_layout().controls;
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= controls.size()) {
    return;
  }

  auto& control = controls[selected_control_index_];
  xe::hid::touch::IOSTouchLayoutSpace position_space = TouchSafeAreaSpaceForView(self);
  xe::hid::touch::IOSTouchLayoutSpace size_space =
      TouchControlSizeSpaceForControlType(self, control.type);
  if (position_space.IsEmpty() || size_space.IsEmpty()) {
    return;
  }

  const bool size_adjust_is_portrait = TouchOverlayIsPortraitForView(self);
  const xe::hid::touch::IOSTouchRect& size_active_source =
      xe::hid::touch::ActiveControlFrameForOrientation(control, size_adjust_is_portrait);
  xe::hid::touch::IOSTouchRect resolved =
      ResolveNormalizedControlFrame(size_active_source, position_space, size_space, control.type);
  const float center_x = resolved.x + resolved.width * 0.5f;
  const float center_y = resolved.y + resolved.height * 0.5f;
  xe::hid::touch::IOSTouchRect candidate = resolved;
  candidate.width *= scale;
  candidate.height *= scale;
  candidate.x = center_x - candidate.width * 0.5f;
  candidate.y = center_y - candidate.height * 0.5f;
  [self beginEditLayoutChangeIfNeeded];
  xe::hid::touch::IOSTouchRect snapped =
      [self snappedResolvedFrameForControlIndex:selected_control_index_
                                 candidateFrame:candidate
                                       safeArea:position_space
                                    gestureMode:TouchCaptureState::EditGestureMode::kResize
                            preserveAspectRatio:YES
                                 preserveCenter:YES];
  xe::hid::touch::IOSTouchRect& size_active_frame =
      xe::hid::touch::MutableActiveControlFrameForOrientation(control, size_adjust_is_portrait);
  size_active_frame =
      NormalizedControlFrameFromResolvedFrame(snapped, position_space, size_space, control.type);
  const xe::hid::touch::IOSTouchRect committed_size_frame = size_active_frame;

  for (TouchCaptureState& capture : active_captures_) {
    if (capture.control_index == selected_control_index_) {
      capture.normalized_frame_at_capture = committed_size_frame;
      capture.anchor_point = capture.current_point;
    }
  }
  if (active_captures_.empty() && !edit_pinch_active_) {
    [self clearEditSnapGuides];
  }

  [self syncControlViewDefinitions];
  [self setNeedsLayout];
  [self layoutIfNeeded];
  [self applyCaptureVisualState];
  [self finishEditLayoutChangeIfNeeded];
}

- (void)adjustSelectedControlOpacityByDelta:(float)delta {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  [self clearEditSnapGuides];
  auto& controls = runtime_model_->mutable_layout().controls;
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= controls.size()) {
    return;
  }

  auto& control = controls[selected_control_index_];
  [self beginEditLayoutChangeIfNeeded];
  control.visual_opacity = std::clamp(control.visual_opacity + delta, 0.15f, 1.0f);
  [self syncControlViewDefinitions];
  [self refreshEditChromeSelection];
  [self setNeedsLayout];
  [self layoutIfNeeded];
  [self applyCaptureVisualState];
  [self finishEditLayoutChangeIfNeeded];
}

- (void)cycleSelectedControlAction {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  [self clearEditSnapGuides];
  auto& controls = runtime_model_->mutable_layout().controls;
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= controls.size()) {
    return;
  }

  auto& control = controls[selected_control_index_];
  if (control.type != xe::hid::touch::IOSTouchControlType::kActionButton) {
    return;
  }

  [self beginEditLayoutChangeIfNeeded];
  xe::hid::touch::ConfigureIOSTouchControlAction(
      xe::hid::touch::NextEditableIOSTouchAction(control.action), &control);
  [self syncControlViewDefinitions];
  [self refreshEditChromeSelection];
  [self refreshEditPreview];
  [self applyCaptureVisualState];
  [self publishResolvedState];
  [self finishEditLayoutChangeIfNeeded];
}

- (void)cycleSelectedControlTint {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  [self clearEditSnapGuides];
  auto& controls = runtime_model_->mutable_layout().controls;
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= controls.size()) {
    return;
  }

  auto& control = controls[selected_control_index_];
  [self beginEditLayoutChangeIfNeeded];
  control.tint_style = xe::hid::touch::NextIOSTouchTintStyle(control.tint_style);
  [self syncControlViewDefinitions];
  [self refreshEditChromeSelection];
  [self refreshEditPreview];
  [self applyCaptureVisualState];
  [self finishEditLayoutChangeIfNeeded];
}

- (void)cycleSelectedControlShape {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  [self clearEditSnapGuides];
  auto& controls = runtime_model_->mutable_layout().controls;
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= controls.size()) {
    return;
  }

  auto& control = controls[selected_control_index_];
  if (control.type != xe::hid::touch::IOSTouchControlType::kActionButton) {
    return;
  }

  [self beginEditLayoutChangeIfNeeded];
  if (control.shape == xe::hid::touch::IOSTouchControlShape::kCircle) {
    control.shape = xe::hid::touch::IOSTouchControlShape::kRoundedRect;
  } else {
    control.shape = xe::hid::touch::IOSTouchControlShape::kCircle;
  }

  [self syncControlViewDefinitions];
  [self refreshEditChromeSelection];
  [self setNeedsLayout];
  [self layoutIfNeeded];
  [self applyCaptureVisualState];
  [self publishResolvedState];
  [self finishEditLayoutChangeIfNeeded];
}

- (void)addNewActionButton {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  auto& layout = runtime_model_->mutable_layout();
  if (layout.controls.size() >= xe::hid::touch::kMaxIOSTouchControls) {
    return;
  }

  [self clearEditSnapGuides];
  [self beginEditLayoutChangeIfNeeded];
  std::string selected_identifier;
  if (!xe::hid::touch::AddSuggestedActionButtonToIOSTouchLayout(
          &layout, TouchOverlayIsPortraitForView(self), &selected_identifier)) {
    [self finishEditLayoutChangeIfNeeded];
    return;
  }
  [self refreshLayoutModel];
  [self selectControlWithIdentifier:selected_identifier];
  [self publishResolvedState];
  [self finishEditLayoutChangeIfNeeded];
}

- (void)mirrorSelectedControlHorizontally {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  [self clearEditSnapGuides];
  auto& layout = runtime_model_->mutable_layout();
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= layout.controls.size()) {
    return;
  }

  [self beginEditLayoutChangeIfNeeded];
  if (!xe::hid::touch::MirrorIOSTouchLayoutControlHorizontally(
          &layout, selected_control_index_, TouchOverlayIsPortraitForView(self))) {
    [self finishEditLayoutChangeIfNeeded];
    return;
  }
  const auto& control = layout.controls[selected_control_index_];
  [self refreshLayoutModel];
  [self selectControlWithIdentifier:control.identifier];
  [self publishResolvedState];
  [self finishEditLayoutChangeIfNeeded];
}

- (void)copyAllControlFramesAcrossOrientationsFromLandscape:(BOOL)from_landscape {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  [self clearEditSnapGuides];
  auto& layout = runtime_model_->mutable_layout();
  if (layout.controls.empty()) {
    return;
  }

  std::string selected_identifier;
  if (selected_control_index_ != NSNotFound && selected_control_index_ < layout.controls.size()) {
    selected_identifier = layout.controls[selected_control_index_].identifier;
  }

  [self beginEditLayoutChangeIfNeeded];
  if (!xe::hid::touch::CopyIOSTouchLayoutFramesAcrossOrientations(&layout, from_landscape)) {
    [self finishEditLayoutChangeIfNeeded];
    return;
  }

  [self refreshLayoutModel];
  if (!selected_identifier.empty()) {
    [self selectControlWithIdentifier:selected_identifier];
  }
  [self publishResolvedState];
  [self finishEditLayoutChangeIfNeeded];
}

- (void)toggleSelectedControlMoveDpadRing {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  [self clearEditSnapGuides];
  auto& layout = runtime_model_->mutable_layout();
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= layout.controls.size()) {
    return;
  }

  auto& control = layout.controls[selected_control_index_];
  if (control.type != xe::hid::touch::IOSTouchControlType::kMoveStick) {
    return;
  }

  [self beginEditLayoutChangeIfNeeded];
  control.move_with_dpad_ring = !control.move_with_dpad_ring;
  [self syncControlViewDefinitions];
  [self refreshEditChromeSelection];
  [self refreshEditPreview];
  [self setNeedsLayout];
  [self publishResolvedState];
  [self finishEditLayoutChangeIfNeeded];
}

- (void)duplicateSelectedControl {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  [self clearEditSnapGuides];
  auto& layout = runtime_model_->mutable_layout();
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= layout.controls.size()) {
    return;
  }
  if (layout.controls.size() >= xe::hid::touch::kMaxIOSTouchControls) {
    return;
  }

  const auto& source_control = layout.controls[selected_control_index_];
  if (source_control.type != xe::hid::touch::IOSTouchControlType::kActionButton) {
    return;
  }

  [self beginEditLayoutChangeIfNeeded];
  std::string selected_identifier;
  if (!xe::hid::touch::DuplicateIOSTouchLayoutActionButton(&layout, selected_control_index_,
                                                           TouchOverlayIsPortraitForView(self),
                                                           &selected_identifier)) {
    [self finishEditLayoutChangeIfNeeded];
    return;
  }
  [self refreshLayoutModel];
  [self selectControlWithIdentifier:selected_identifier];
  [self publishResolvedState];
  [self finishEditLayoutChangeIfNeeded];
}

- (void)deleteSelectedControl {
  if (!editing_controls_enabled_ || !runtime_model_) {
    return;
  }

  [self clearEditSnapGuides];
  auto& layout = runtime_model_->mutable_layout();
  if (selected_control_index_ == NSNotFound || selected_control_index_ >= layout.controls.size()) {
    return;
  }
  if (layout.controls.size() <= 1) {
    return;
  }

  [self beginEditLayoutChangeIfNeeded];
  if (!xe::hid::touch::DeleteIOSTouchLayoutControl(&layout, selected_control_index_)) {
    [self finishEditLayoutChangeIfNeeded];
    return;
  }
  selected_control_index_ = NSNotFound;
  [self refreshLayoutModel];
  [self publishResolvedState];
  [self finishEditLayoutChangeIfNeeded];
}

- (void)updateControlFrameForCapture:(TouchCaptureState&)capture newPoint:(CGPoint)new_point {
  if (!editing_controls_enabled_ || !runtime_model_ || capture.control_index == NSNotFound) {
    return;
  }

  auto& controls = runtime_model_->mutable_layout().controls;
  if (capture.control_index >= controls.size()) {
    return;
  }
  xe::hid::touch::IOSTouchControlDefinition& drag_control = controls[capture.control_index];
  xe::hid::touch::IOSTouchLayoutSpace position_space = TouchSafeAreaSpaceForView(self);
  xe::hid::touch::IOSTouchLayoutSpace size_space =
      TouchControlSizeSpaceForControlType(self, drag_control.type);
  if (position_space.IsEmpty() || size_space.IsEmpty()) {
    return;
  }

  [self beginEditLayoutChangeIfNeeded];
  xe::hid::touch::IOSTouchRect next_frame = ResolveNormalizedControlFrame(
      capture.normalized_frame_at_capture, position_space, size_space, drag_control.type);
  if (capture.edit_gesture_mode == TouchCaptureState::EditGestureMode::kResize) {
    next_frame.width += static_cast<float>(new_point.x - capture.anchor_point.x);
    next_frame.height += static_cast<float>(new_point.y - capture.anchor_point.y);
  } else {
    next_frame.x += static_cast<float>(new_point.x - capture.anchor_point.x);
    next_frame.y += static_cast<float>(new_point.y - capture.anchor_point.y);
  }
  xe::hid::touch::IOSTouchRect snapped =
      [self snappedResolvedFrameForControlIndex:capture.control_index
                                 candidateFrame:next_frame
                                       safeArea:position_space
                                    gestureMode:capture.edit_gesture_mode
                            preserveAspectRatio:NO
                                 preserveCenter:NO];
  // Drag math operates on the orientation-resolved baseline that was
  // captured at touchesBegan; route the commit back through the same
  // orientation choke point so portrait drags only mutate the portrait
  // override (and lazily promote it the very first time).
  const bool drag_commit_is_portrait = TouchOverlayIsPortraitForView(self);
  xe::hid::touch::IOSTouchRect& drag_active_frame =
      xe::hid::touch::MutableActiveControlFrameForOrientation(drag_control,
                                                              drag_commit_is_portrait);
  drag_active_frame = NormalizedControlFrameFromResolvedFrame(snapped, position_space, size_space,
                                                              drag_control.type);
}

- (void)applyCaptureVisualState {
  if (!runtime_model_) {
    move_knob_.hidden = YES;
    return;
  }

  const auto& controls = runtime_model_->layout().controls;
  const NSUInteger control_count =
      MIN(control_views_.count, static_cast<NSUInteger>(controls.size()));
  if (visually_active_control_indices_.size() != control_count) {
    visually_active_control_indices_.resize(control_count);
  }
  std::fill(visually_active_control_indices_.begin(), visually_active_control_indices_.end(), 0);
  const TouchCaptureState* move_capture = nullptr;

  for (const TouchCaptureState& capture : active_captures_) {
    if (capture.control_index >= control_count) {
      continue;
    }
    visually_active_control_indices_[capture.control_index] = 1;
    if (capture.control_index == move_control_index_) {
      move_capture = &capture;
    }
  }

  if (editing_controls_enabled_ && selected_control_index_ != NSNotFound &&
      selected_control_index_ < control_count) {
    visually_active_control_indices_[selected_control_index_] = 1;
  }

  for (NSUInteger control_index = 0; control_index < control_views_.count; ++control_index) {
    const BOOL active =
        (control_index < control_count && visually_active_control_indices_[control_index]) ? YES
                                                                                           : NO;
    [[control_views_ objectAtIndex:control_index] setTouchActive:active];
  }

  if (editing_controls_enabled_) {
    move_knob_.hidden = YES;
    return;
  }

  if (!move_capture || move_control_index_ == NSNotFound ||
      move_control_index_ >= resolved_control_frames_.size()) {
    move_knob_.hidden = YES;
    return;
  }

  const xe::hid::touch::IOSTouchRect& move_frame = resolved_control_frames_[move_control_index_];
  const xe::hid::touch::IOSTouchControlDefinition& move_control = controls[move_control_index_];
  const CGFloat outer_radius =
      MIN(move_frame.width, move_frame.height) * MAX(move_control.activation_radius, 0.24f);
  CGPoint delta = CGPointMake(move_capture->current_point.x - move_capture->anchor_point.x,
                              move_capture->current_point.y - move_capture->anchor_point.y);
  const CGFloat distance = std::hypot(delta.x, delta.y);
  if (distance > outer_radius && distance > 0.0f) {
    const CGFloat scale = outer_radius / distance;
    delta.x *= scale;
    delta.y *= scale;
  }

  const CGFloat knob_size = MIN(move_frame.width, move_frame.height) * 0.28f;
  move_knob_.bounds = CGRectMake(0.0f, 0.0f, knob_size, knob_size);
  move_knob_.center =
      CGPointMake(move_capture->anchor_point.x + delta.x, move_capture->anchor_point.y + delta.y);
  move_knob_.layer.cornerRadius = knob_size * 0.5f;
  move_knob_.hidden = NO;
}

- (void)publishResolvedState {
  if (!runtime_model_) {
    return;
  }

  xe::hid::touch::IOSTouchResolvedState state = {};
  state.gameplay_enabled = gameplay_overlay_active_ && !editing_controls_enabled_;

  if (editing_controls_enabled_) {
    if (!xe::hid::touch::TouchStatesEqualIgnoringPacket(state, last_published_state_)) {
      state.packet_number = next_packet_number_++;
      if (next_packet_number_ == 0) {
        next_packet_number_ = 1;
      }
      last_published_state_ = state;
    } else {
      state.packet_number = last_published_state_.packet_number;
    }

    runtime_model_->StoreResolvedState(state);
    [self applyCaptureVisualState];
    return;
  }

  const auto& controls = runtime_model_->layout().controls;
  const NSUInteger control_count =
      MIN(control_views_.count, static_cast<NSUInteger>(controls.size()));
  const CFTimeInterval current_time = CACurrentMediaTime();

  for (const TouchCaptureState& capture : active_captures_) {
    if (capture.control_index >= control_count ||
        capture.control_index >= resolved_control_frames_.size()) {
      continue;
    }

    const xe::hid::touch::IOSTouchControlDefinition& control = controls[capture.control_index];
    const xe::hid::touch::IOSTouchRect& frame = resolved_control_frames_[capture.control_index];

    switch (control.type) {
      case xe::hid::touch::IOSTouchControlType::kMoveStick: {
        // Move + D-Pad combo: a touch that landed on one of the four arrow
        // zones drives the corresponding D-Pad bit (press-and-hold while the
        // finger remains on the arrow) instead of the analog stick. The
        // sub-zone is decided once at touchesBegan and pinned for the
        // capture's lifetime, so a finger drifting toward the centre doesn't
        // silently turn into a stick deflection.
        switch (capture.combo_subzone) {
          case TouchCaptureState::ComboSubzone::kDpadUp:
            state.buttons |= xe::hid::X_INPUT_GAMEPAD_DPAD_UP;
            continue;
          case TouchCaptureState::ComboSubzone::kDpadDown:
            state.buttons |= xe::hid::X_INPUT_GAMEPAD_DPAD_DOWN;
            continue;
          case TouchCaptureState::ComboSubzone::kDpadLeft:
            state.buttons |= xe::hid::X_INPUT_GAMEPAD_DPAD_LEFT;
            continue;
          case TouchCaptureState::ComboSubzone::kDpadRight:
            state.buttons |= xe::hid::X_INPUT_GAMEPAD_DPAD_RIGHT;
            continue;
          case TouchCaptureState::ComboSubzone::kStick:
          case TouchCaptureState::ComboSubzone::kNone:
            break;
        }
        const CGPoint move_unit = MoveStickUnitVectorForCapture(control, frame, capture);
        if (!CGPointEqualToPoint(move_unit, CGPointZero)) {
          const float normalized_x = static_cast<float>(move_unit.x);
          const float normalized_y = static_cast<float>(move_unit.y);
          // Move-style controls can be configured with action=kLook to drive
          // the right thumbstick (FPS aim) rather than the left thumbstick
          // (movement). The visual + capture pipeline is identical; only the
          // output target changes. The per-control look_sensitivity_scale
          // (relative_look_scale) tunes the aim ramp.
          if (control.action == xe::hid::touch::IOSTouchAction::kLook) {
            const float look_scale = std::clamp(control.relative_look_scale, 0.25f, 4.0f);
            state.thumb_rx = xe::hid::touch::TouchAxisFromUnit(normalized_x * look_scale);
            state.thumb_ry = xe::hid::touch::TouchAxisFromUnit(-normalized_y * look_scale);
          } else {
            state.thumb_lx = xe::hid::touch::TouchAxisFromUnit(normalized_x);
            state.thumb_ly = xe::hid::touch::TouchAxisFromUnit(-normalized_y);
          }
        }
      } break;

      case xe::hid::touch::IOSTouchControlType::kActionButton: {
        const TouchInteractionBehaviorState secondary_behavior_state =
            ResolveTouchInteractionBehaviorState(control.secondary_behavior, capture, current_time);
        const bool touch_active = control.hold_while_captured || secondary_behavior_state.active ||
                                  TouchControlContainsPoint(control, frame, capture.current_point);
        if (touch_active) {
          if (!xe::hid::touch::TouchControlUsesDeferredPrimaryTap(control)) {
            xe::hid::touch::ApplyTouchActionMapping(control, &state);
          }
          if (secondary_behavior_state.active &&
              control.secondary_behavior.action != xe::hid::touch::IOSTouchAction::kNone) {
            xe::hid::touch::ApplyTouchActionMappingForAction(control.secondary_behavior.action,
                                                             &state);
          }
          if (!control.hold_while_captured &&
              !xe::hid::touch::TouchControlUsesDeferredPrimaryTap(control) &&
              capture.control_index < recent_action_press_times_.size()) {
            recent_action_press_times_[capture.control_index] = current_time;
          }
        }
      } break;

      case xe::hid::touch::IOSTouchControlType::kLookSwipeZone:
      case xe::hid::touch::IOSTouchControlType::kPauseButton:
      default:
        break;
    }
  }

  const float button_tap_hold_seconds = TouchButtonTapHoldSeconds();
  for (NSUInteger control_index = 0; control_index < control_count; ++control_index) {
    const xe::hid::touch::IOSTouchControlDefinition& control = controls[control_index];
    if (control.type != xe::hid::touch::IOSTouchControlType::kActionButton ||
        control.hold_while_captured || control_index >= recent_action_press_times_.size()) {
      continue;
    }

    if ((current_time - recent_action_press_times_[control_index]) < button_tap_hold_seconds) {
      xe::hid::touch::ApplyTouchActionMapping(control, &state);
    }
  }

  for (NSUInteger control_index = 0; control_index < control_count; ++control_index) {
    const xe::hid::touch::IOSTouchControlDefinition& control = controls[control_index];
    if (control.secondary_behavior.action == xe::hid::touch::IOSTouchAction::kNone ||
        control_index >= recent_secondary_press_times_.size()) {
      continue;
    }

    if ((current_time - recent_secondary_press_times_[control_index]) < button_tap_hold_seconds) {
      xe::hid::touch::ApplyTouchActionMappingForAction(control.secondary_behavior.action, &state);
    }
  }

  // Accumulate look contributions across every control with a recent motion
  // entry instead of picking only the freshest. The previous "freshest wins"
  // approach trampled the look output whenever two simultaneously look-capable
  // controls (e.g. an on-screen look swipe zone and an action button with
  // enables_relative_look) overlapped — the more recent one would silently
  // overwrite the other. Per-axis max-magnitude blending lets both contribute
  // (the one moving harder wins per axis) while still allowing the per-control
  // hold/decay tail to keep low-FPS gameplay polls observing transient look
  // motions.
  const float look_hold_seconds = TouchLookHoldSeconds();
  CGPoint accumulated_right_thumb = CGPointZero;  // for action == kLook (default)
  CGPoint accumulated_left_thumb = CGPointZero;   // for action == kMove (cross-typed)
  bool any_left_swipe = false;
  const NSUInteger look_state_count =
      MIN(control_count, static_cast<NSUInteger>(recent_look_motion_times_.size()));
  for (NSUInteger control_index = 0; control_index < look_state_count; ++control_index) {
    const CFTimeInterval motion_time = recent_look_motion_times_[control_index];
    if (motion_time <= 0.0) {
      continue;
    }
    const CFTimeInterval look_motion_age = current_time - motion_time;
    if (look_motion_age >= look_hold_seconds) {
      continue;
    }
    const float decay =
        std::clamp(1.0f - static_cast<float>(look_motion_age / look_hold_seconds), 0.0f, 1.0f);
    const CGPoint vector = recent_look_vectors_[control_index];
    const CGFloat decayed_x = vector.x * decay;
    const CGFloat decayed_y = vector.y * decay;
    // Route swipe-style contributions based on the source control's action:
    // kMove → left thumbstick (rare, but enables a swipe-to-move overlay);
    // kLook (or anything else look-capable like an action button with
    // enables_relative_look) → right thumbstick.
    const bool routes_to_left =
        controls[control_index].action == xe::hid::touch::IOSTouchAction::kMove;
    CGPoint& accumulator = routes_to_left ? accumulated_left_thumb : accumulated_right_thumb;
    if (std::abs(decayed_x) > std::abs(accumulator.x)) {
      accumulator.x = decayed_x;
    }
    if (std::abs(decayed_y) > std::abs(accumulator.y)) {
      accumulator.y = decayed_y;
    }
    if (routes_to_left && (decayed_x != 0.0 || decayed_y != 0.0)) {
      any_left_swipe = true;
    }
  }
  // Look output: max-magnitude blend swipe contribution with whatever the
  // per-capture loop above already wrote (e.g. a MoveStick configured with
  // action=kLook).
  const int16_t swipe_rx =
      xe::hid::touch::TouchAxisFromUnit(static_cast<float>(accumulated_right_thumb.x));
  const int16_t swipe_ry =
      xe::hid::touch::TouchAxisFromUnit(static_cast<float>(accumulated_right_thumb.y));
  if (std::abs(static_cast<int>(swipe_rx)) > std::abs(static_cast<int>(state.thumb_rx))) {
    state.thumb_rx = swipe_rx;
  }
  if (std::abs(static_cast<int>(swipe_ry)) > std::abs(static_cast<int>(state.thumb_ry))) {
    state.thumb_ry = swipe_ry;
  }
  // Move output via swipe (LookSwipeZone configured with action=kMove): only
  // override the per-capture left thumb if a swipe contribution actually
  // exists, so we don't stomp on a normal MoveStick capture.
  if (any_left_swipe) {
    const int16_t swipe_lx =
        xe::hid::touch::TouchAxisFromUnit(static_cast<float>(accumulated_left_thumb.x));
    const int16_t swipe_ly =
        xe::hid::touch::TouchAxisFromUnit(static_cast<float>(accumulated_left_thumb.y));
    if (std::abs(static_cast<int>(swipe_lx)) > std::abs(static_cast<int>(state.thumb_lx))) {
      state.thumb_lx = swipe_lx;
    }
    if (std::abs(static_cast<int>(swipe_ly)) > std::abs(static_cast<int>(state.thumb_ly))) {
      state.thumb_ly = swipe_ly;
    }
  }

  if (!xe::hid::touch::TouchStatesEqualIgnoringPacket(state, last_published_state_)) {
    state.packet_number = next_packet_number_++;
    if (next_packet_number_ == 0) {
      next_packet_number_ = 1;
    }
    last_published_state_ = state;
  } else {
    state.packet_number = last_published_state_.packet_number;
  }

  runtime_model_->StoreResolvedState(state);
  [self applyCaptureVisualState];
}

- (void)setGameplayOverlayVisible:(BOOL)visible animated:(BOOL)animated {
  gameplay_overlay_active_ = visible;

  if (visible) {
    if (self.hidden) {
      self.alpha = 0.0;
    }
    self.hidden = NO;
    self.userInteractionEnabled = YES;
    display_link_.paused = NO;
    [self publishResolvedState];
    if (!animated) {
      self.alpha = 1.0;
      return;
    }
    // Spring-style entrance: subtle bounce so the overlay arrives with
    // physical weight rather than a flat fade.
    UIViewPropertyAnimator* show_animator = [[UIViewPropertyAnimator alloc] initWithDuration:0.35
                                                                                dampingRatio:0.85
                                                                                  animations:^{
                                                                                    self.alpha =
                                                                                        1.0;
                                                                                  }];
    [show_animator startAnimation];
    [show_animator release];
    return;
  }

  display_link_.paused = YES;
  self.userInteractionEnabled = NO;
  [self resetInteractionState];
  [self publishResolvedState];

  if (!animated || self.hidden) {
    self.hidden = YES;
    self.alpha = 0.0;
    return;
  }

  UIViewPropertyAnimator* hide_animator = [[UIViewPropertyAnimator alloc] initWithDuration:0.22
                                                                              dampingRatio:1.0
                                                                                animations:^{
                                                                                  self.alpha = 0.0;
                                                                                }];
  [hide_animator addCompletion:^(__unused UIViewAnimatingPosition position) {
    self.hidden = YES;
  }];
  [hide_animator startAnimation];
  [hide_animator release];
}

- (void)displayLinkFired:(CADisplayLink*)__unused display_link {
  [self publishResolvedState];
}

- (UIView*)hitTest:(CGPoint)point withEvent:(UIEvent*)event {
  if (self.hidden || self.alpha <= 0.01f || !self.userInteractionEnabled ||
      !gameplay_overlay_active_) {
    return nil;
  }

  if (editing_controls_enabled_) {
    UIView* chrome_hit = [edit_chrome_ interactiveHitTestForOverlayPoint:point
                                                                   event:event
                                                                  inView:self];
    if (chrome_hit) {
      return chrome_hit;
    }
    if (edit_showing_layout_library_) {
      if (CGRectContainsPoint(edit_chrome_.frame, point)) {
        return edit_chrome_;
      }
      return nil;
    }
    if (CGRectContainsPoint([self selectedControlResizeHandleFrame], point)) {
      return self;
    }
    if (CGRectContainsPoint([self editChromeHeaderDragFrame], point)) {
      return self;
    }
    if (CGRectContainsPoint(edit_chrome_.frame, point)) {
      return edit_chrome_;
    }
  }

  if (!editing_controls_enabled_ && pause_button_ && !pause_button_.hidden) {
    CGPoint pause_point = [pause_button_ convertPoint:point fromView:self];
    UIView* pause_hit = [pause_button_ hitTest:pause_point withEvent:event];
    if (pause_hit) {
      return pause_hit;
    }
  }

  if (!runtime_model_) {
    return nil;
  }

  const auto& controls = runtime_model_->layout().controls;
  const NSUInteger control_count =
      MIN(control_views_.count, static_cast<NSUInteger>(controls.size()));
  for (NSUInteger control_index = 0; control_index < control_count; ++control_index) {
    if (control_index >= resolved_control_frames_.size() ||
        (!editing_controls_enabled_ && control_index == pause_control_index_)) {
      continue;
    }
    if (TouchControlContainsPoint(controls[control_index], resolved_control_frames_[control_index],
                                  point)) {
      return self;
    }
  }

  return nil;
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)__unused event {
  if (!runtime_model_ || !gameplay_overlay_active_) {
    return;
  }

  const auto& controls = runtime_model_->layout().controls;
  const NSUInteger control_count =
      MIN(control_views_.count, static_cast<NSUInteger>(controls.size()));
  for (UITouch* touch in touches) {
    CGPoint point = [touch locationInView:self];
    if (editing_controls_enabled_ && CGRectContainsPoint([self editChromeHeaderDragFrame], point)) {
      if (!edit_pinch_active_ && active_captures_.empty()) {
        edit_chrome_drag_active_ = YES;
        edit_chrome_drag_touch_ = touch;
        edit_chrome_drag_frame_ = edit_chrome_.frame;
        edit_chrome_drag_touch_offset_ = CGPointMake(point.x - CGRectGetMinX(edit_chrome_.frame),
                                                     point.y - CGRectGetMinY(edit_chrome_.frame));
        [self clearEditSnapGuides];
      }
      continue;
    }
    if (editing_controls_enabled_ && edit_pinch_active_) {
      continue;
    }
    if (editing_controls_enabled_ && [self tryBeginEditPinchWithTouch:touch atPoint:point]) {
      continue;
    }
    if (editing_controls_enabled_ && selected_control_index_ != NSNotFound &&
        CGRectContainsPoint([self selectedControlResizeHandleFrame], point)) {
      TouchCaptureState capture;
      capture.touch = touch;
      capture.control_index = selected_control_index_;
      capture.anchor_point = point;
      capture.current_point = point;
      capture.began_time = CACurrentMediaTime();
      const bool resize_capture_is_portrait = TouchOverlayIsPortraitForView(self);
      capture.normalized_frame_at_capture = xe::hid::touch::ActiveControlFrameForOrientation(
          controls[capture.control_index], resize_capture_is_portrait);
      capture.edit_gesture_mode = TouchCaptureState::EditGestureMode::kResize;
      [self beginEditLayoutChangeIfNeeded];
      active_captures_.push_back(capture);
      [self clearEditSnapGuides];
      continue;
    }

    NSInteger best_control_index = -1;
    uint8_t best_priority = 0;

    for (NSUInteger control_index = 0; control_index < control_count; ++control_index) {
      if (control_index >= resolved_control_frames_.size() ||
          (!editing_controls_enabled_ && control_index == pause_control_index_) ||
          [self isControlIndexCaptured:control_index]) {
        continue;
      }

      const xe::hid::touch::IOSTouchControlDefinition& control = controls[control_index];
      if (!TouchControlContainsPoint(control, resolved_control_frames_[control_index], point)) {
        continue;
      }

      if (best_control_index < 0 || control.capture_priority > best_priority) {
        best_control_index = static_cast<NSInteger>(control_index);
        best_priority = control.capture_priority;
      }
    }

    if (best_control_index < 0) {
      continue;
    }

    TouchCaptureState capture;
    capture.touch = touch;
    capture.control_index = static_cast<NSUInteger>(best_control_index);
    capture.anchor_point = point;
    capture.current_point = point;
    capture.began_time = CACurrentMediaTime();
    // Capture the orientation-active frame so subsequent drag math walks
    // off the right baseline. The drag commit (updateControlFrameForCapture)
    // writes back through MutableActiveControlFrameForOrientation to
    // preserve the per-orientation split.
    const bool capture_is_portrait = TouchOverlayIsPortraitForView(self);
    capture.normalized_frame_at_capture = xe::hid::touch::ActiveControlFrameForOrientation(
        controls[capture.control_index], capture_is_portrait);
    capture.edit_gesture_mode = TouchCaptureState::EditGestureMode::kMove;
    // For Move + D-Pad combos: pin the touch to one of {stick, up, down, left,
    // right} at touchesBegan and keep it there for the capture's lifetime. The
    // publish path uses this to decide between analog stick output and a
    // discrete D-Pad bit. For non-combo controls the helper returns kStick
    // (treated as default-MoveStick / no-op for other types).
    capture.combo_subzone = TouchComboSubzoneForPoint(
        controls[capture.control_index], resolved_control_frames_[capture.control_index], point);
    if (editing_controls_enabled_) {
      [self clearEditSnapGuides];
      [self setSelectedControlIndex:capture.control_index];
      [self beginEditLayoutChangeIfNeeded];
    } else {
      // Press haptic on gameplay captures only — editor drags use the
      // selection haptic via setSelectedControlIndex above. Look swipe zones
      // are continuous and would buzz constantly, so they do not trigger.
      switch (controls[capture.control_index].type) {
        case xe::hid::touch::IOSTouchControlType::kActionButton:
        case xe::hid::touch::IOSTouchControlType::kPauseButton:
          [self playPressHaptic];
          break;
        case xe::hid::touch::IOSTouchControlType::kMoveStick:
          // D-Pad arrow taps on a combo control feel like a button press,
          // not a stick engage. Use the medium press haptic for arrow zones
          // and reserve the lighter haptic for the analog stick centre.
          if (capture.combo_subzone == TouchCaptureState::ComboSubzone::kDpadUp ||
              capture.combo_subzone == TouchCaptureState::ComboSubzone::kDpadDown ||
              capture.combo_subzone == TouchCaptureState::ComboSubzone::kDpadLeft ||
              capture.combo_subzone == TouchCaptureState::ComboSubzone::kDpadRight) {
            [self playPressHaptic];
          } else {
            [self playLightPressHaptic];
          }
          break;
        case xe::hid::touch::IOSTouchControlType::kLookSwipeZone:
        default:
          break;
      }
    }
    active_captures_.push_back(capture);
  }

  [self publishResolvedState];
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)__unused event {
  if (!runtime_model_ || !gameplay_overlay_active_) {
    return;
  }

  const auto& controls = runtime_model_->layout().controls;
  const CFTimeInterval current_time = CACurrentMediaTime();
  for (UITouch* touch in touches) {
    if (editing_controls_enabled_ && edit_chrome_drag_active_ && touch == edit_chrome_drag_touch_) {
      xe::hid::touch::IOSTouchLayoutSpace safe_area = TouchSafeAreaSpaceForView(self);
      if (!safe_area.IsEmpty()) {
        CGPoint new_point = [touch locationInView:self];
        edit_chrome_drag_frame_.origin =
            CGPointMake(new_point.x - edit_chrome_drag_touch_offset_.x,
                        new_point.y - edit_chrome_drag_touch_offset_.y);
        edit_chrome_drag_frame_ = [self clampedEditChromeFrame:edit_chrome_drag_frame_
                                                      safeArea:safe_area];
        [self setNeedsLayout];
        [self layoutIfNeeded];
      }
      continue;
    }
    if (editing_controls_enabled_ && edit_pinch_active_ &&
        (touch == edit_pinch_touch_a_ || touch == edit_pinch_touch_b_)) {
      [self updatePinchedControlFrame];
      continue;
    }

    auto capture_it =
        std::find_if(active_captures_.begin(), active_captures_.end(),
                     [touch](const TouchCaptureState& capture) { return capture.touch == touch; });
    if (capture_it == active_captures_.end() || capture_it->control_index >= controls.size()) {
      continue;
    }

    CGPoint new_point = [touch locationInView:self];
    if (editing_controls_enabled_) {
      [self updateControlFrameForCapture:*capture_it newPoint:new_point];
      capture_it->current_point = new_point;
      continue;
    }

    const xe::hid::touch::IOSTouchControlDefinition& control = controls[capture_it->control_index];
    TouchCaptureState behavior_capture = *capture_it;
    behavior_capture.current_point = new_point;
    if (control.type == xe::hid::touch::IOSTouchControlType::kMoveStick &&
        control.secondary_behavior.trigger ==
            xe::hid::touch::IOSTouchInteractionTrigger::kDoubleTapForward &&
        capture_it->control_index < resolved_control_frames_.size() &&
        !capture_it->secondary_behavior_triggered &&
        [self hasPendingDoubleTapCandidateForControlIndex:capture_it->control_index
                                                   atTime:current_time] &&
        MoveStickCaptureQualifiesForDoubleTapForward(
            control, resolved_control_frames_[capture_it->control_index], behavior_capture,
            current_time)) {
      capture_it->secondary_behavior_triggered =
          [self consumeDoubleTapCandidateForControlIndex:capture_it->control_index
                                                  atTime:current_time];
    }
    const TouchInteractionBehaviorState secondary_behavior_state =
        ResolveTouchInteractionBehaviorState(control.secondary_behavior, behavior_capture,
                                             current_time);
    if (capture_it->control_index == look_control_index_ || control.enables_relative_look ||
        secondary_behavior_state.enables_relative_look) {
      const CGPoint delta = CGPointMake(new_point.x - capture_it->current_point.x,
                                        new_point.y - capture_it->current_point.y);
      const float look_scale = capture_it->control_index == look_control_index_
                                   ? std::clamp(control.relative_look_scale, 0.25f, 4.0f)
                                   : (control.enables_relative_look
                                          ? std::clamp(control.relative_look_scale, 0.25f, 4.0f)
                                          : secondary_behavior_state.relative_look_scale);
      [self storeLookMotion:SwipeLookVectorForDelta(delta, look_scale)
            forControlIndex:capture_it->control_index
                     atTime:current_time];
    }
    capture_it->current_point = new_point;
  }

  if (editing_controls_enabled_) {
    [self setNeedsLayout];
    [self layoutIfNeeded];
    [self applyCaptureVisualState];
    [self publishResolvedState];
    return;
  }

  [self publishResolvedState];
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)__unused event {
  [self finalizeTouches:touches cancelled:NO];
}

- (void)finalizeTouches:(NSSet<UITouch*>*)touches cancelled:(BOOL)cancelled {
  if (!runtime_model_) {
    return;
  }

  UITouch* pinch_remaining_touch = nil;
  const bool ended_pinch_a = edit_pinch_active_ && [touches containsObject:edit_pinch_touch_a_];
  const bool ended_pinch_b = edit_pinch_active_ && [touches containsObject:edit_pinch_touch_b_];
  if (edit_pinch_active_) {
    if (ended_pinch_a && !ended_pinch_b) {
      pinch_remaining_touch = edit_pinch_touch_b_;
    } else if (ended_pinch_b && !ended_pinch_a) {
      pinch_remaining_touch = edit_pinch_touch_a_;
    }
  }

  const auto& controls = runtime_model_->layout().controls;
  const CFTimeInterval current_time = CACurrentMediaTime();
  for (UITouch* touch in touches) {
    if (editing_controls_enabled_ && edit_chrome_drag_active_ && touch == edit_chrome_drag_touch_) {
      if (!cancelled) {
        xe::hid::touch::IOSTouchLayoutSpace safe_area = TouchSafeAreaSpaceForView(self);
        if (!safe_area.IsEmpty()) {
          edit_chrome_dock_index_ =
              [self nearestEditChromeDockIndexForFrame:edit_chrome_drag_frame_
                                              safeArea:safe_area
                                                 width:CGRectGetWidth(edit_chrome_drag_frame_)
                                                height:CGRectGetHeight(edit_chrome_drag_frame_)];
          [[NSUserDefaults standardUserDefaults] setInteger:edit_chrome_dock_index_
                                                     forKey:@"XeniaTouchEditChromeDock"];
        }
      }
      [self clearEditChromeDragState];
      [self setNeedsLayout];
      [self layoutIfNeeded];
      continue;
    }
    auto capture_it =
        std::find_if(active_captures_.begin(), active_captures_.end(),
                     [touch](const TouchCaptureState& capture) { return capture.touch == touch; });
    if (capture_it == active_captures_.end()) {
      continue;
    }
    const CGPoint release_point = [touch locationInView:self];
    capture_it->current_point = release_point;
    if (cancelled) {
      [self clearLookMotionStateForControlIndex:capture_it->control_index];
    }
    if (!cancelled && !editing_controls_enabled_ && capture_it->control_index < controls.size() &&
        capture_it->control_index < resolved_control_frames_.size()) {
      const NSUInteger control_index = capture_it->control_index;
      const xe::hid::touch::IOSTouchControlDefinition& control = controls[control_index];
      const xe::hid::touch::IOSTouchRect& frame = resolved_control_frames_[control_index];
      switch (control.type) {
        case xe::hid::touch::IOSTouchControlType::kActionButton: {
          const bool ended_inside = TouchControlContainsPoint(control, frame, release_point);
          if (xe::hid::touch::TouchControlUsesDeferredPrimaryTap(control) &&
              control_index < recent_action_press_times_.size()) {
            const TouchInteractionBehaviorState secondary_behavior_state =
                ResolveTouchInteractionBehaviorState(control.secondary_behavior, *capture_it,
                                                     current_time);
            if (ended_inside && !secondary_behavior_state.active) {
              recent_action_press_times_[control_index] = current_time;
            }
          }

          const bool quick_tap = (current_time - capture_it->began_time) <=
                                 [self doubleTapWindowSecondsForControl:control];
          if (ended_inside && quick_tap &&
              control.secondary_behavior.trigger ==
                  xe::hid::touch::IOSTouchInteractionTrigger::kDoubleTap &&
              xe::hid::touch::TouchInteractionBehaviorConfigured(control.secondary_behavior)) {
            if (![self consumeDoubleTapCandidateForControlIndex:control_index
                                                         atTime:current_time]) {
              [self storeDoubleTapCandidateForControlIndex:control_index atTime:current_time];
            }
          }
        } break;

        case xe::hid::touch::IOSTouchControlType::kMoveStick: {
          if (control.secondary_behavior.trigger ==
                  xe::hid::touch::IOSTouchInteractionTrigger::kDoubleTapForward &&
              xe::hid::touch::TouchInteractionBehaviorConfigured(control.secondary_behavior) &&
              !capture_it->secondary_behavior_triggered &&
              MoveStickCaptureQualifiesForDoubleTapForward(control, frame, *capture_it,
                                                           current_time)) {
            if (![self consumeDoubleTapCandidateForControlIndex:control_index
                                                         atTime:current_time]) {
              [self storeDoubleTapCandidateForControlIndex:control_index atTime:current_time];
            }
          }
        } break;

        case xe::hid::touch::IOSTouchControlType::kLookSwipeZone:
        case xe::hid::touch::IOSTouchControlType::kPauseButton:
        default:
          break;
      }
    }
    active_captures_.erase(capture_it);
  }

  if (edit_pinch_active_ && (ended_pinch_a || ended_pinch_b)) {
    [self endEditPinchRetainingTouch:(cancelled ? nil : pinch_remaining_touch)];
  } else if (editing_controls_enabled_ && active_captures_.empty()) {
    [self clearEditSnapGuides];
  }

  if (editing_controls_enabled_) {
    [self finishEditLayoutChangeIfNeeded];
  }

  if (editing_controls_enabled_) {
    [self setNeedsLayout];
    [self layoutIfNeeded];
  }
  [self publishResolvedState];
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)__unused event {
  [self finalizeTouches:touches cancelled:YES];
}

- (void)layoutSubviews {
  [super layoutSubviews];

  if (!runtime_model_) {
    return;
  }

  xe::hid::touch::IOSTouchLayoutSpace safe_area = TouchSafeAreaSpaceForView(self);
  edit_snap_guides_overlay_.frame = self.bounds;
  edit_grid_overlay_.frame = CGRectIntegral(CGRectFromTouchRect(xe::hid::touch::IOSTouchRect{
      safe_area.origin_x, safe_area.origin_y, safe_area.width, safe_area.height}));
  edit_safe_area_guide_.frame = CGRectIntegral(CGRectFromTouchRect(xe::hid::touch::IOSTouchRect{
      safe_area.origin_x, safe_area.origin_y, safe_area.width, safe_area.height}));
  edit_snap_guides_layer_.frame = edit_snap_guides_overlay_.bounds;
  edit_grid_dots_layer_.frame = edit_grid_overlay_.bounds;
  UIBezierPath* dot_path = [UIBezierPath bezierPath];
  if (CGRectGetWidth(edit_grid_overlay_.bounds) > 0.0 &&
      CGRectGetHeight(edit_grid_overlay_.bounds) > 0.0) {
    for (CGFloat y = kEditGridSpacingPoints * 0.5f; y < CGRectGetHeight(edit_grid_overlay_.bounds);
         y += kEditGridSpacingPoints) {
      for (CGFloat x = kEditGridSpacingPoints * 0.5f; x < CGRectGetWidth(edit_grid_overlay_.bounds);
           x += kEditGridSpacingPoints) {
        [dot_path appendPath:[UIBezierPath
                                 bezierPathWithOvalInRect:CGRectMake(x - kEditGridDotRadius,
                                                                     y - kEditGridDotRadius,
                                                                     kEditGridDotRadius * 2.0f,
                                                                     kEditGridDotRadius * 2.0f)]];
      }
    }
  }
  edit_grid_dots_layer_.path = dot_path.CGPath;

  const auto& controls = runtime_model_->layout().controls;
  const NSUInteger control_count =
      MIN(control_views_.count, static_cast<NSUInteger>(controls.size()));
  resolved_control_frames_.resize(control_count);
  pause_button_.hidden = pause_control_index_ == NSNotFound || editing_controls_enabled_;
  // Single source of truth for the orientation lookup — every per-frame
  // resolve below feeds resolved_control_frames_, which is what hit-testing,
  // snap targets, the edit chrome, and publishResolvedState all read.
  const bool layout_is_portrait = TouchOverlayIsPortraitForView(self);
  const BOOL orientation_flipped =
      !last_layout_orientation_known_ ||
      static_cast<BOOL>(layout_is_portrait) != last_layout_was_portrait_;
  last_layout_was_portrait_ = static_cast<BOOL>(layout_is_portrait);
  last_layout_orientation_known_ = YES;
  for (NSUInteger control_index = 0; control_index < control_count; ++control_index) {
    XeniaTouchControlShellView* control_view = [control_views_ objectAtIndex:control_index];
    const xe::hid::touch::IOSTouchControlDefinition& control = controls[control_index];
    const xe::hid::touch::IOSTouchRect& active_frame =
        xe::hid::touch::ActiveControlFrameForOrientation(control, layout_is_portrait);
    xe::hid::touch::IOSTouchLayoutSpace size_space =
        TouchControlSizeSpaceForControlType(self, control.type);
    xe::hid::touch::IOSTouchRect frame =
        ResolveNormalizedControlFrame(active_frame, safe_area, size_space, control.type);
    resolved_control_frames_[control_index] = frame;
    control_view.frame = CGRectIntegral(CGRectFromTouchRect(frame));
    if (!editing_controls_enabled_ && control_index == pause_control_index_) {
      pause_button_.frame = control_view.frame;
      pause_button_.hidden = NO;
    }
    // Full-screen Look swipe zones go fully invisible during gameplay so the
    // player's view of the game isn't obscured. The shell still receives
    // touches; only the visible chrome is suppressed. In edit mode the user
    // needs to be able to see the zone they're configuring, so keep it
    // visible there.
    const bool is_fullscreen_look =
        control.type == xe::hid::touch::IOSTouchControlType::kLookSwipeZone &&
        active_frame.width >= 0.95f && active_frame.height >= 0.95f;
    [control_view setChromeSuppressed:(is_fullscreen_look && !editing_controls_enabled_)];
  }

  const CGFloat chrome_margin = 14.0f;
  const BOOL chrome_minimized = edit_chrome_minimized_;
  const BOOL showing_layout_library = edit_showing_layout_library_ && !chrome_minimized;
  const CGFloat chrome_width =
      MIN(safe_area.width - chrome_margin * 2.0f,
          chrome_minimized ? 316.0f : (showing_layout_library ? 540.0f : 320.0f));
  const CGFloat chrome_height = [edit_chrome_ preferredHeightForWidth:chrome_width
                                                      availableHeight:safe_area.height
                                                               margin:chrome_margin];
  edit_chrome_.frame = [self resolvedEditChromeFrameForSafeArea:safe_area
                                                          width:chrome_width
                                                         height:chrome_height];
  [edit_chrome_ setNeedsLayout];

  if (chrome_minimized) {
    edit_resize_handle_.frame = CGRectIntegral([self selectedControlResizeHandleFrame]);
    edit_resize_handle_.hidden =
        !editing_controls_enabled_ || selected_control_index_ == NSNotFound;
    [self updateConflictHighlights];
    [self updateEditSnapGuidesPath];
    [self applyCaptureVisualState];
    return;
  }

  if (showing_layout_library) {
    edit_resize_handle_.frame = CGRectZero;
    edit_resize_handle_.hidden = YES;
    [self updateConflictHighlights];
    [self updateEditSnapGuidesPath];
    [self applyCaptureVisualState];
    return;
  }

  edit_resize_handle_.frame = CGRectIntegral([self selectedControlResizeHandleFrame]);
  edit_resize_handle_.hidden = !editing_controls_enabled_ || selected_control_index_ == NSNotFound;

  [self updateConflictHighlights];
  [self updateEditSnapGuidesPath];
  [self applyCaptureVisualState];

  // After rotation, re-sync the edit chrome chip + the More-menu copy-action
  // label so they reflect the orientation now being edited. This is cheap
  // (UI text + UIMenu rebuild) and only fires when orientation actually
  // flips, not on every layout pass.
  if (orientation_flipped && editing_controls_enabled_) {
    [self refreshEditChromeSelection];
  }
}

- (void)pauseButtonPressed:(UIButton*)__unused sender {
  if (pauseHandler_) {
    pauseHandler_();
  }
}

- (void)touchOverlayEditChromeDidRequestDoneEditing:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  if (doneEditingHandler_) {
    doneEditingHandler_();
  }
}

- (void)touchOverlayEditChromeDidRequestSmallerControl:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  [self adjustSelectedControlSizeByScale:0.90f];
}

- (void)touchOverlayEditChromeDidRequestMatchNearestSize:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  [self matchSelectedControlSizeToNearestSibling];
}

- (void)touchOverlayEditChromeDidRequestLargerControl:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  [self adjustSelectedControlSizeByScale:1.10f];
}

- (void)touchOverlayEditChromeDidRequestDimmerControl:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  [self adjustSelectedControlOpacityByDelta:-0.08f];
}

- (void)touchOverlayEditChromeDidRequestBolderControl:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  [self adjustSelectedControlOpacityByDelta:0.08f];
}

- (void)touchOverlayEditChromeDidRequestCollapse:(XeniaTouchOverlayEditChromeIOS*)__unused chrome {
  xe::hid::touch::IOSTouchLayoutSpace safe_area = TouchSafeAreaSpaceForView(self);
  if (!safe_area.IsEmpty()) {
    edit_chrome_dock_index_ =
        [self nearestEditChromeDockIndexForFrame:edit_chrome_.frame
                                        safeArea:safe_area
                                           width:CGRectGetWidth(edit_chrome_.frame)
                                          height:CGRectGetHeight(edit_chrome_.frame)];
  }
  edit_chrome_minimized_ = !edit_chrome_minimized_;
  [self clearEditChromeDragState];
  [self refreshEditChromeSelection];
  [self refreshEditPreview];
  [self setNeedsLayout];
  [UIView animateWithDuration:0.16
                   animations:^{
                     [self layoutIfNeeded];
                   }];
}

- (void)touchOverlayEditChromeDidRequestCycleAction:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  [self cycleSelectedControlAction];
}

- (void)touchOverlayEditChromeDidRequestRenameLabel:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  [self presentLabelRenameAlert];
}

- (void)touchOverlayEditChromeDidRequestCycleTint:(XeniaTouchOverlayEditChromeIOS*)__unused chrome {
  [self cycleSelectedControlTint];
}

- (void)touchOverlayEditChromeDidRequestCycleShape:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  [self cycleSelectedControlShape];
}

- (void)touchOverlayEditChromeDidRequestDuplicateControl:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  [self duplicateSelectedControl];
}

- (void)touchOverlayEditChromeDidRequestUndo:(XeniaTouchOverlayEditChromeIOS*)__unused chrome {
  [self undoEditLayoutChange];
}

- (void)touchOverlayEditChromeDidRequestRedo:(XeniaTouchOverlayEditChromeIOS*)__unused chrome {
  [self redoEditLayoutChange];
}

- (void)touchOverlayEditChromeDidRequestToggleGrid:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  edit_grid_enabled_ = !edit_grid_enabled_;
  if (editing_controls_enabled_ && edit_grid_enabled_) {
    edit_grid_overlay_.hidden = NO;
  }
  [UIView animateWithDuration:0.12
      animations:^{
        edit_grid_overlay_.alpha = (editing_controls_enabled_ && edit_grid_enabled_) ? 1.0 : 0.0;
      }
      completion:^(__unused BOOL finished) {
        edit_grid_overlay_.hidden = !editing_controls_enabled_ || !edit_grid_enabled_;
      }];
  [self refreshEditChromeSelection];
  [self setNeedsLayout];
}

- (void)touchOverlayEditChromeDidRequestAddDefaultControl:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  [self addNewActionButton];
}

- (void)touchOverlayEditChromeDidRequestDeleteControl:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  [self deleteSelectedControl];
}

- (void)touchOverlayEditChromeDidRequestLayoutLibrary:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  if (layoutLibraryHandler_) {
    layoutLibraryHandler_();
  }
}

- (void)touchOverlayEditChromeDidRequestHideLayoutLibrary:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  [self hideLayoutLibrary];
}

- (void)touchOverlayEditChromeDidRequestLayoutLibrarySaveCopy:
    (XeniaTouchOverlayEditChromeIOS*)__unused chrome {
  if (layoutLibrarySaveCopyHandler_) {
    layoutLibrarySaveCopyHandler_();
  }
}

- (void)touchOverlayEditChromeDidRequestLayoutLibraryRename:
    (XeniaTouchOverlayEditChromeIOS*)__unused chrome {
  if (layoutLibraryRenameHandler_) {
    layoutLibraryRenameHandler_();
  }
}

- (void)touchOverlayEditChromeDidRequestLayoutLibraryDelete:
    (XeniaTouchOverlayEditChromeIOS*)__unused chrome {
  if (layoutLibraryDeleteHandler_) {
    layoutLibraryDeleteHandler_();
  }
}

- (void)touchOverlayEditChromeDidRequestLayoutLibraryImport:
    (XeniaTouchOverlayEditChromeIOS*)__unused chrome {
  if (layoutLibraryImportHandler_) {
    layoutLibraryImportHandler_();
  }
}

- (void)touchOverlayEditChromeDidRequestLayoutLibraryExport:
    (XeniaTouchOverlayEditChromeIOS*)__unused chrome {
  if (layoutLibraryExportHandler_) {
    layoutLibraryExportHandler_();
  }
}

- (void)touchOverlayEditChromeDidRequestLayoutLibraryReset:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  if (layoutLibraryResetHandler_) {
    layoutLibraryResetHandler_();
  }
}

- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)__unused chrome
    didRequestLayoutLibraryLoad:(NSString*)localID {
  if (layoutLibraryLoadHandler_) {
    layoutLibraryLoadHandler_(localID);
  }
}

- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)__unused chrome
              didRequestAction:(xe::hid::touch::IOSTouchAction)action {
  [self setSelectedControlAction:action];
}

- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)__unused chrome
         didRequestLabelHidden:(BOOL)hidden {
  [self setSelectedControlLabelHidden:hidden];
}

- (void)touchOverlayEditChromeDidRequestResetLabel:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  [self resetSelectedControlLabel];
}

- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)__unused chrome
     didRequestBehaviorTrigger:(xe::hid::touch::IOSTouchInteractionTrigger)trigger {
  [self setSelectedControlBehaviorTrigger:trigger];
}

- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)__unused chrome
      didRequestBehaviorAction:(xe::hid::touch::IOSTouchAction)action {
  [self setSelectedControlBehaviorAction:action];
}

- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)__unused chrome
           didRequestTintStyle:(xe::hid::touch::IOSTouchTintStyle)tintStyle {
  [self setSelectedControlTintStyle:tintStyle];
}

- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)__unused chrome
           didRequestLookScale:(float)lookScale {
  [self setSelectedControlLookScale:lookScale];
}

- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)__unused chrome
               didRequestShape:(xe::hid::touch::IOSTouchControlShape)shape {
  [self setSelectedControlShape:shape];
}

- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)__unused chrome
    didRequestCopyLayoutFromLandscape:(BOOL)fromLandscape {
  [self copyAllControlFramesAcrossOrientationsFromLandscape:fromLandscape];
}

- (void)touchOverlayEditChromeDidRequestMirrorControl:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  [self mirrorSelectedControlHorizontally];
}

- (void)touchOverlayEditChromeDidRequestToggleMoveDpadRing:(XeniaTouchOverlayEditChromeIOS*)__unused
    chrome {
  [self toggleSelectedControlMoveDpadRing];
}

- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)__unused chrome
    didRequestAddControlOfType:(xe::hid::touch::IOSTouchControlType)type {
  [self addControlOfType:type];
}

@end
