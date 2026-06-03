/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#import "xenia/ui/ios/touch/touch_overlay_edit_chrome_ios.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

#import "xenia/ui/ios/shared/ios_theme_controls.h"
#import "xenia/ui/ios/shared/ios_view_helpers.h"
#import "xenia/ui/ios/touch/touch_controls_overlay_helpers_ios.h"
#import "xenia/ui/ios/touch/touch_layout_library_controller_ios.h"
#include "xenia/ui/ios/touch/touch_overlay_style_ios.h"

namespace {

using xe::ui::XeniaTouchConfiguredControlLabelText;
using namespace xe::ui::ios::touch_overlay;

constexpr CGFloat kChromeInset = 14.0f;
constexpr CGFloat kHeaderTop = 10.0f;
constexpr CGFloat kHeaderButtonHeight = 28.0f;
constexpr CGFloat kHeaderGap = 8.0f;
constexpr CGFloat kDoneWidth = 56.0f;
constexpr CGFloat kLayoutsWidth = 64.0f;
constexpr CGFloat kGridWidth = 48.0f;
constexpr CGFloat kCollapseWidth = 48.0f;
constexpr CGFloat kRowGap = 6.0f;
constexpr CGFloat kButtonHeight = 32.0f;

UIButton* CreateChromeButton(NSString* title, id target, SEL selector) {
  UIButton* button = [[UIButton buttonWithType:UIButtonTypeSystem] retain];
  button.hidden = YES;
  // The touch overlay editor renders over the game view (which is full-bleed
  // dark game content), so white-on-dark with token opacities reads more
  // legibly than the theme's bgSurface tints. The high-alpha title (0.94)
  // stays a literal — it's the "almost solid text" band that the current
  // token scale doesn't model and isn't worth a one-off token.
  button.backgroundColor =
      [[UIColor whiteColor] colorWithAlphaComponent:[XeniaTheme opacitySoft]];
  button.layer.cornerRadius = XeniaRadiusLg;
  xe_apply_button_title_font(button, UIFontTextStyleFootnote, 13.0,
                             UIFontWeightSemibold);
  button.titleLabel.adjustsFontSizeToFitWidth = YES;
  button.titleLabel.minimumScaleFactor = 0.60f;
  [button setTitleColor:[[UIColor whiteColor] colorWithAlphaComponent:0.94]
               forState:UIControlStateNormal];
  [button setTitle:title forState:UIControlStateNormal];
  button.accessibilityLabel = title;
  button.accessibilityTraits = UIAccessibilityTraitButton;
  [button addTarget:target action:selector forControlEvents:UIControlEventTouchUpInside];
  return button;
}

void ConfigureMenuButton(UIButton* button, UIMenu* menu) {
  button.menu = menu;
  button.showsMenuAsPrimaryAction = menu != nil;
}

}  // namespace

@implementation XeniaTouchOverlayEditChromeIOS {
  id<XeniaTouchOverlayEditChromeIOSDelegate> delegate_;
  UILabel* title_label_;
  UILabel* selection_label_;
  UILabel* preview_label_;
  UIButton* smaller_button_;
  UIButton* match_size_button_;
  UIButton* larger_button_;
  UIButton* dimmer_button_;
  UIButton* bolder_button_;
  UIButton* action_button_;
  UIButton* label_button_;
  UIButton* behavior_button_;
  UIButton* color_button_;
  UIButton* shape_button_;
  UIButton* duplicate_button_;
  UIButton* undo_button_;
  UIButton* redo_button_;
  UIButton* collapse_button_;
  UIButton* grid_button_;
  UIButton* add_button_;
  UIButton* delete_button_;
  UIButton* library_button_;
  UIButton* done_button_;
  UITableView* layout_library_table_;
  XeniaTouchLayoutLibraryTableController* layout_library_controller_;
  xe::ui::ios::touch_overlay::TouchOverlayEditChromeState state_;
}

@synthesize delegate = delegate_;

- (instancetype)initWithFrame:(CGRect)frame {
  if (!(self = [super initWithFrame:frame])) {
    return nil;
  }

  self.hidden = YES;
  xe_apply_floating_window_chrome(self);

  title_label_ = [[UILabel alloc] initWithFrame:CGRectZero];
  title_label_.backgroundColor = [UIColor clearColor];
  title_label_.text = @"Edit";
  xe_apply_label_font(title_label_, UIFontTextStyleBody, 16.0,
                      UIFontWeightSemibold);
  title_label_.textColor = [[UIColor whiteColor] colorWithAlphaComponent:0.94];
  title_label_.adjustsFontSizeToFitWidth = YES;
  title_label_.minimumScaleFactor = 0.70f;
  title_label_.accessibilityTraits = UIAccessibilityTraitHeader;
  [self addSubview:title_label_];

  selection_label_ = [[UILabel alloc] initWithFrame:CGRectZero];
  selection_label_.backgroundColor = [UIColor clearColor];
  xe_apply_label_font(selection_label_, UIFontTextStyleSubheadline, 15.0,
                      UIFontWeightSemibold);
  selection_label_.textColor = [[UIColor whiteColor] colorWithAlphaComponent:0.82];
  selection_label_.adjustsFontSizeToFitWidth = YES;
  selection_label_.minimumScaleFactor = 0.75f;
  [self addSubview:selection_label_];

  preview_label_ = [[UILabel alloc] initWithFrame:CGRectZero];
  preview_label_.backgroundColor =
      [[UIColor whiteColor] colorWithAlphaComponent:[XeniaTheme opacitySubtle]];
  preview_label_.layer.cornerRadius = XeniaRadiusLg;
  preview_label_.layer.borderWidth = 1.0f;
  preview_label_.layer.borderColor =
      [[UIColor whiteColor] colorWithAlphaComponent:[XeniaTheme opacitySoft]].CGColor;
  preview_label_.layer.masksToBounds = YES;
  xe_apply_monospaced_label_font(preview_label_, UIFontTextStyleCaption2, 10.0,
                                 UIFontWeightMedium);
  preview_label_.textColor = [[UIColor whiteColor] colorWithAlphaComponent:0.72];
  preview_label_.numberOfLines = 0;
  [self addSubview:preview_label_];

  smaller_button_ = CreateChromeButton(@"Smaller", self, @selector(smallerButtonPressed:));
  [self addSubview:smaller_button_];
  match_size_button_ = CreateChromeButton(@"Match Size", self, @selector(matchSizeButtonPressed:));
  [self addSubview:match_size_button_];
  larger_button_ = CreateChromeButton(@"Larger", self, @selector(largerButtonPressed:));
  [self addSubview:larger_button_];
  dimmer_button_ = CreateChromeButton(@"Dimmer", self, @selector(dimmerButtonPressed:));
  [self addSubview:dimmer_button_];
  bolder_button_ = CreateChromeButton(@"Bolder", self, @selector(bolderButtonPressed:));
  [self addSubview:bolder_button_];
  action_button_ = CreateChromeButton(@"Binding", self, @selector(actionButtonPressed:));
  [self addSubview:action_button_];
  label_button_ = CreateChromeButton(@"Label", self, @selector(labelButtonPressed:));
  [self addSubview:label_button_];
  behavior_button_ = CreateChromeButton(@"Behavior: Off", self, @selector(behaviorButtonPressed:));
  behavior_button_.titleLabel.minimumScaleFactor = 0.68f;
  [self addSubview:behavior_button_];
  color_button_ = CreateChromeButton(@"Color", self, @selector(colorButtonPressed:));
  [self addSubview:color_button_];
  shape_button_ = CreateChromeButton(@"Shape", self, @selector(shapeButtonPressed:));
  [self addSubview:shape_button_];
  duplicate_button_ = CreateChromeButton(@"Duplicate", self, @selector(duplicateButtonPressed:));
  [self addSubview:duplicate_button_];
  undo_button_ = CreateChromeButton(@"Undo", self, @selector(undoButtonPressed:));
  [self addSubview:undo_button_];
  redo_button_ = CreateChromeButton(@"Redo", self, @selector(redoButtonPressed:));
  [self addSubview:redo_button_];
  collapse_button_ = CreateChromeButton(@"Hide", self, @selector(collapseButtonPressed:));
  [self addSubview:collapse_button_];
  grid_button_ = CreateChromeButton(@"Grid: Off", self, @selector(gridButtonPressed:));
  grid_button_.showsMenuAsPrimaryAction = NO;
  [self addSubview:grid_button_];
  add_button_ = CreateChromeButton(@"New Button", self, @selector(addButtonPressed:));
  [self addSubview:add_button_];
  delete_button_ = CreateChromeButton(@"Delete", self, @selector(deleteButtonPressed:));
  // Destructive: pinkish error tint. statusError lightened toward white for
  // legibility on the dark overlay background.
  [delete_button_ setTitleColor:[[XeniaTheme statusError] colorWithAlphaComponent:0.96]
                       forState:UIControlStateNormal];
  [self addSubview:delete_button_];
  library_button_ = CreateChromeButton(@"Layouts", self, @selector(layoutLibraryButtonPressed:));
  [self addSubview:library_button_];
  done_button_ = CreateChromeButton(@"Done", self, @selector(doneEditingButtonPressed:));
  // Done is the affirmative primary action; amber matches the in-game touch
  // accent so the colour reads as "you'll see these controls when you tap".
  done_button_.backgroundColor =
      [[XeniaTheme touchTintAmber] colorWithAlphaComponent:0.95];
  xe_apply_button_title_font(done_button_, UIFontTextStyleSubheadline, 14.0,
                             UIFontWeightSemibold);
  done_button_.accessibilityHint = @"Exits touch control editing.";
  [done_button_ setTitleColor:[UIColor blackColor] forState:UIControlStateNormal];
  [self addSubview:done_button_];

  layout_library_table_ = [[UITableView alloc] initWithFrame:CGRectZero
                                                       style:UITableViewStylePlain];
  layout_library_table_.hidden = YES;
  layout_library_table_.backgroundColor = [UIColor clearColor];
  layout_library_table_.separatorStyle = UITableViewCellSeparatorStyleNone;
  layout_library_table_.separatorInset = UIEdgeInsetsMake(0, 16, 0, 16);
  layout_library_table_.rowHeight = 52.0f;
  layout_library_table_.estimatedRowHeight = 52.0f;
  layout_library_table_.estimatedSectionHeaderHeight = 24.0f;
  layout_library_table_.estimatedSectionFooterHeight = 0.01f;
  if (@available(iOS 15.0, *)) {
    layout_library_table_.sectionHeaderTopPadding = 0.0f;
  }
  layout_library_table_.separatorColor =
      [[UIColor whiteColor] colorWithAlphaComponent:[XeniaTheme opacitySoft]];
  if (@available(iOS 11.0, *)) {
    layout_library_table_.contentInsetAdjustmentBehavior = UIScrollViewContentInsetAdjustmentNever;
  }
  layout_library_controller_ = [[XeniaTouchLayoutLibraryTableController alloc] init];
  __unsafe_unretained XeniaTouchOverlayEditChromeIOS* unsafe_self = self;
  layout_library_controller_.saveCopyHandler = ^{
    [unsafe_self->delegate_ touchOverlayEditChromeDidRequestLayoutLibrarySaveCopy:unsafe_self];
  };
  layout_library_controller_.importHandler = ^{
    [unsafe_self->delegate_ touchOverlayEditChromeDidRequestLayoutLibraryImport:unsafe_self];
  };
  layout_library_controller_.exportHandler = ^{
    [unsafe_self->delegate_ touchOverlayEditChromeDidRequestLayoutLibraryExport:unsafe_self];
  };
  layout_library_controller_.resetHandler = ^{
    [unsafe_self->delegate_ touchOverlayEditChromeDidRequestLayoutLibraryReset:unsafe_self];
  };
  layout_library_controller_.renameHandler = ^{
    [unsafe_self->delegate_ touchOverlayEditChromeDidRequestLayoutLibraryRename:unsafe_self];
  };
  layout_library_controller_.deleteHandler = ^{
    [unsafe_self->delegate_ touchOverlayEditChromeDidRequestLayoutLibraryDelete:unsafe_self];
  };
  layout_library_controller_.loadHandler = ^(NSString* local_id) {
    [unsafe_self->delegate_ touchOverlayEditChrome:unsafe_self
                       didRequestLayoutLibraryLoad:local_id];
  };
  layout_library_table_.dataSource = layout_library_controller_;
  layout_library_table_.delegate = layout_library_controller_;
  layout_library_table_.indicatorStyle = UIScrollViewIndicatorStyleWhite;
  [self addSubview:layout_library_table_];

  return self;
}

- (void)dealloc {
  delegate_ = nil;
  [layout_library_controller_ release];
  [layout_library_table_ release];
  [done_button_ release];
  [library_button_ release];
  [delete_button_ release];
  [add_button_ release];
  [grid_button_ release];
  [collapse_button_ release];
  [redo_button_ release];
  [undo_button_ release];
  [duplicate_button_ release];
  [shape_button_ release];
  [color_button_ release];
  [behavior_button_ release];
  [label_button_ release];
  [action_button_ release];
  [bolder_button_ release];
  [dimmer_button_ release];
  [larger_button_ release];
  [match_size_button_ release];
  [smaller_button_ release];
  [preview_label_ release];
  [selection_label_ release];
  [title_label_ release];
  [super dealloc];
}

- (void)setLayoutLibraryItems:(NSArray<XeniaTouchLayoutLibraryItem*>*)items
         currentLayoutLocalID:(NSString*)currentLayoutLocalID {
  [layout_library_controller_ setItems:items currentLayoutLocalID:currentLayoutLocalID];
  [layout_library_table_ reloadData];
}

- (NSString*)previewDescriptionForState {
  if (!state_.has_selected_control) {
    return @"Tap a control to inspect its resolved XInput output.";
  }

  const auto& control = state_.selected_control;
  switch (control.type) {
    case xe::hid::touch::IOSTouchControlType::kMoveStick:
      return
          [NSString stringWithFormat:@"LX/LY move\nRadius %.0f%% • Deadzone %.0f%%",
                                     control.activation_radius * 100.0f, control.deadzone * 100.0f];
    case xe::hid::touch::IOSTouchControlType::kLookSwipeZone:
      return [NSString stringWithFormat:@"RX/RY look\n%.1f pt full-scale • Look x%.2f • Y x%.2f • "
                                        @"Hold %.0f ms",
                                        TouchLookPointsPerFullScale() /
                                            std::max(control.relative_look_scale, 0.1f),
                                        control.relative_look_scale, TouchLookVerticalScale(),
                                        TouchLookHoldSeconds() * 1000.0f];
    case xe::hid::touch::IOSTouchControlType::kPauseButton:
      return @"Pause overlay control";
    case xe::hid::touch::IOSTouchControlType::kActionButton: {
      xe::hid::touch::IOSTouchResolvedState sample_state = {};
      xe::hid::touch::ApplyTouchActionMapping(control, &sample_state);
      NSString* buttons_text = TouchButtonMaskPreviewText(sample_state.buttons);
      NSString* look_text =
          control.enables_relative_look
              ? [NSString stringWithFormat:@"Look x%.2f", control.relative_look_scale]
              : @"Look off";
      NSString* mode_text =
          control.action == xe::hid::touch::IOSTouchAction::kNone
              ? @"Unused"
              : (control.hold_while_captured
                     ? @"Primary hold"
                     : (xe::hid::touch::TouchControlUsesDeferredPrimaryTap(control)
                            ? @"Primary tap on release"
                            : [NSString stringWithFormat:@"Primary tap %.0f ms",
                                                         TouchButtonTapHoldSeconds() * 1000.0f]));
      NSString* behavior_detail =
          control.secondary_behavior.trigger != xe::hid::touch::IOSTouchInteractionTrigger::kNone
              ? [NSString
                    stringWithFormat:@"%@ • %@",
                                     TouchInteractionBehaviorSummaryText(
                                         control.secondary_behavior),
                                     control.secondary_behavior.enables_relative_look
                                         ? [NSString stringWithFormat:@"Look x%.2f",
                                                                      control.secondary_behavior
                                                                          .relative_look_scale]
                                         : @"Look off"]
              : @"Behavior: Off";
      return [NSString stringWithFormat:@"Primary %@ • LT/RT %u/%u • %@\n%@\n%@", buttons_text,
                                        sample_state.left_trigger, sample_state.right_trigger,
                                        look_text, mode_text, behavior_detail];
    }
  }
  return @"XInput preview unavailable.";
}

- (BOOL)selectedControlSupportsLookScaleTuning {
  if (!state_.has_selected_control) {
    return NO;
  }
  const auto& control = state_.selected_control;
  return control.type == xe::hid::touch::IOSTouchControlType::kLookSwipeZone ||
         control.enables_relative_look;
}

- (float)selectedControlLookScale {
  if (!state_.has_selected_control) {
    return 1.0f;
  }
  return std::clamp(state_.selected_control.relative_look_scale, 0.25f, 4.0f);
}

- (void)applyState:(const xe::ui::ios::touch_overlay::TouchOverlayEditChromeState&)state {
  state_ = state;

  if (state_.showing_layout_library) {
    const BOOL show_edit_chrome = state_.editing_enabled;
    const BOOL show_minimized_header_only = show_edit_chrome && state_.minimized;
    title_label_.text = @"Layouts";
    title_label_.hidden = !show_edit_chrome;
    selection_label_.text = @"Apply a layout or manage saved copies";
    selection_label_.hidden = !show_edit_chrome || show_minimized_header_only;
    preview_label_.hidden = YES;
    smaller_button_.hidden = YES;
    match_size_button_.hidden = YES;
    larger_button_.hidden = YES;
    dimmer_button_.hidden = YES;
    bolder_button_.hidden = YES;
    action_button_.hidden = YES;
    label_button_.hidden = YES;
    behavior_button_.hidden = YES;
    color_button_.hidden = YES;
    shape_button_.hidden = YES;
    duplicate_button_.hidden = YES;
    undo_button_.hidden = YES;
    redo_button_.hidden = YES;
    collapse_button_.hidden = YES;
    grid_button_.hidden = YES;
    add_button_.hidden = YES;
    delete_button_.hidden = YES;
    library_button_.hidden = !show_edit_chrome;
    done_button_.hidden = !show_edit_chrome;
    [library_button_ setTitle:@"Back" forState:UIControlStateNormal];
    layout_library_table_.hidden = !show_edit_chrome || show_minimized_header_only;
    [self setNeedsLayout];
    return;
  }

  NSString* selection_text = @"Tap a control";
  NSString* chrome_title_text = @"Edit";
  BOOL has_selection = NO;
  BOOL can_remap_action = NO;
  BOOL can_edit_behavior = NO;
  NSString* action_button_title = @"Binding";
  NSString* behavior_button_title = @"Behavior: Off";
  NSString* color_button_title = @"Style";
  NSString* duplicate_button_title = @"More";

  if (state_.has_selected_control) {
    const auto& control = state_.selected_control;
    NSString* label_text = XeniaTouchConfiguredControlLabelText(control, YES);
    chrome_title_text = state_.minimized
                            ? [NSString stringWithFormat:@"Edit %@", label_text ?: @"Control"]
                            : @"Edit";
    selection_text = [NSString stringWithFormat:@"Selected • %@", label_text ?: @"Control"];
    has_selection = YES;
    can_remap_action = control.type == xe::hid::touch::IOSTouchControlType::kActionButton;
    can_edit_behavior =
        can_remap_action || control.type == xe::hid::touch::IOSTouchControlType::kMoveStick;
    NSString* action_name =
        [NSString stringWithUTF8String:xe::hid::touch::IOSTouchActionDisplayName(control.action)];
    action_button_title = can_remap_action ? [NSString stringWithFormat:@"Binding: %@", action_name]
                                           : @"Binding Locked";
    if (can_edit_behavior) {
      behavior_button_title = TouchInteractionBehaviorSummaryText(control.secondary_behavior);
    }
  }

  NSString* orientation_chip = state_.editing_portrait ? @"Portrait" : @"Landscape";
  chrome_title_text = [NSString stringWithFormat:@"%@ · %@", chrome_title_text, orientation_chip];

  const BOOL show_edit_chrome = state_.editing_enabled;
  const BOOL show_add_button = show_edit_chrome;
  const BOOL show_minimized_header_only = show_edit_chrome && state_.minimized;
  title_label_.text = chrome_title_text;
  title_label_.hidden = !show_edit_chrome;
  selection_label_.hidden = !show_edit_chrome || show_minimized_header_only;
  preview_label_.hidden = !show_edit_chrome || show_minimized_header_only || !can_remap_action;
  smaller_button_.hidden = YES;
  match_size_button_.hidden = YES;
  larger_button_.hidden = YES;
  dimmer_button_.hidden = YES;
  bolder_button_.hidden = YES;
  action_button_.hidden = !show_edit_chrome || show_minimized_header_only || !can_remap_action;
  label_button_.hidden = YES;
  behavior_button_.hidden = !show_edit_chrome || show_minimized_header_only || !can_edit_behavior;
  color_button_.hidden = !show_edit_chrome || show_minimized_header_only || !has_selection;
  shape_button_.hidden = YES;
  duplicate_button_.hidden = !show_edit_chrome || show_minimized_header_only || !has_selection;
  undo_button_.hidden = !show_edit_chrome || show_minimized_header_only;
  redo_button_.hidden = !show_edit_chrome || show_minimized_header_only;
  collapse_button_.hidden = !show_edit_chrome;
  grid_button_.hidden = !show_edit_chrome;
  add_button_.hidden = !show_add_button || show_minimized_header_only;
  delete_button_.hidden = YES;
  library_button_.hidden = !show_edit_chrome;
  done_button_.hidden = !show_edit_chrome;

  selection_label_.text = selection_text;
  preview_label_.text = [self previewDescriptionForState];
  smaller_button_.enabled = NO;
  match_size_button_.enabled = NO;
  larger_button_.enabled = NO;
  dimmer_button_.enabled = NO;
  bolder_button_.enabled = NO;
  smaller_button_.alpha = 0.0;
  match_size_button_.alpha = 0.0;
  larger_button_.alpha = 0.0;
  dimmer_button_.alpha = 0.0;
  bolder_button_.alpha = 0.0;
  action_button_.enabled = can_remap_action;
  action_button_.alpha = can_remap_action ? 1.0 : 0.45;
  [action_button_ setTitle:action_button_title forState:UIControlStateNormal];
  label_button_.enabled = NO;
  label_button_.alpha = 0.0;
  behavior_button_.enabled = can_edit_behavior;
  behavior_button_.alpha = can_edit_behavior ? 1.0 : 0.45;
  [behavior_button_ setTitle:behavior_button_title forState:UIControlStateNormal];
  color_button_.enabled = has_selection;
  color_button_.alpha = has_selection ? 1.0 : 0.45;
  [color_button_ setTitle:color_button_title forState:UIControlStateNormal];
  shape_button_.enabled = NO;
  shape_button_.alpha = 0.0;
  duplicate_button_.enabled = has_selection;
  duplicate_button_.alpha = has_selection ? 1.0 : 0.45;
  [duplicate_button_ setTitle:duplicate_button_title forState:UIControlStateNormal];
  undo_button_.enabled = state_.can_undo;
  undo_button_.alpha = undo_button_.enabled ? 1.0 : 0.45;
  [undo_button_ setTitle:@"Undo" forState:UIControlStateNormal];
  redo_button_.enabled = state_.can_redo;
  redo_button_.alpha = redo_button_.enabled ? 1.0 : 0.45;
  [redo_button_ setTitle:@"Redo" forState:UIControlStateNormal];
  [collapse_button_ setTitle:(state_.minimized ? @"Edit" : @"Hide") forState:UIControlStateNormal];
  collapse_button_.enabled = show_edit_chrome;
  collapse_button_.alpha = show_edit_chrome ? 1.0 : 0.45;
  [grid_button_ setTitle:@"Grid" forState:UIControlStateNormal];
  add_button_.enabled = state_.editing_enabled;
  add_button_.alpha = state_.editing_enabled ? 1.0 : 0.45;
  [add_button_ setTitle:@"New" forState:UIControlStateNormal];
  [library_button_ setTitle:@"Layouts" forState:UIControlStateNormal];
  grid_button_.enabled = state_.editing_enabled;
  grid_button_.alpha = state_.editing_enabled ? 1.0 : 0.45;
  grid_button_.backgroundColor =
      state_.grid_enabled
          ? [[XeniaTheme touchTintAmber] colorWithAlphaComponent:0.95]
          : [[UIColor whiteColor] colorWithAlphaComponent:[XeniaTheme opacitySoft]];
  [grid_button_
      setTitleColor:(state_.grid_enabled
                         ? [UIColor blackColor]
                         : [[UIColor whiteColor] colorWithAlphaComponent:0.94])
           forState:UIControlStateNormal];
  delete_button_.enabled = NO;
  delete_button_.alpha = 0.0;
  layout_library_table_.hidden = YES;
  [self refreshPickerMenus];
  [self setNeedsLayout];
}

- (NSArray<NSArray<UIButton*>*>*)bodyRows {
  return @[
    @[ undo_button_, redo_button_ ],
    @[ action_button_, behavior_button_ ],
    @[ color_button_, duplicate_button_ ],
    @[ add_button_ ],
  ];
}

- (CGFloat)preferredHeightForWidth:(CGFloat)width
                   availableHeight:(CGFloat)availableHeight
                            margin:(CGFloat)margin {
  const BOOL chrome_minimized = state_.minimized;
  const BOOL showing_layout_library = state_.showing_layout_library && !chrome_minimized;
  if (showing_layout_library) {
    return MAX(50.0f, MIN(availableHeight - margin * 2.0f, availableHeight * 0.78f));
  }

  CGFloat chrome_height = chrome_minimized ? 50.0f : (12.0f + 36.0f + 8.0f + 20.0f + 10.0f);
  if (!chrome_minimized) {
    for (NSArray<UIButton*>* row_buttons in [self bodyRows]) {
      BOOL has_visible_button = NO;
      for (UIButton* button in row_buttons) {
        if (!button.hidden) {
          has_visible_button = YES;
          break;
        }
      }
      if (has_visible_button) {
        chrome_height += kButtonHeight + kRowGap;
      }
    }
    if (!preview_label_.hidden) {
      chrome_height +=
          TouchEditPreviewHeightForLabel(preview_label_, width - kChromeInset * 2.0f) + kRowGap;
    }
  }
  return chrome_height + 10.0f;
}

- (void)layoutSubviews {
  [super layoutSubviews];

  const CGFloat local_width = CGRectGetWidth(self.bounds);
  done_button_.frame = CGRectMake(local_width - kChromeInset - kDoneWidth, 8.0f, kDoneWidth, 34.0f);
  library_button_.frame = CGRectMake(CGRectGetMinX(done_button_.frame) - kHeaderGap - kLayoutsWidth,
                                     kHeaderTop, kLayoutsWidth, kHeaderButtonHeight);
  grid_button_.frame = CGRectMake(CGRectGetMinX(library_button_.frame) - kHeaderGap - kGridWidth,
                                  kHeaderTop, kGridWidth, kHeaderButtonHeight);
  collapse_button_.frame =
      CGRectMake(CGRectGetMinX(grid_button_.frame) - kHeaderGap - kCollapseWidth, kHeaderTop,
                 kCollapseWidth, kHeaderButtonHeight);
  title_label_.frame = CGRectMake(
      kChromeInset, 12.0f, CGRectGetMinX(collapse_button_.frame) - kChromeInset - 8.0f, 22.0f);

  if (state_.minimized) {
    selection_label_.frame = CGRectZero;
    action_button_.frame = CGRectZero;
    label_button_.frame = CGRectZero;
    behavior_button_.frame = CGRectZero;
    smaller_button_.frame = CGRectZero;
    match_size_button_.frame = CGRectZero;
    larger_button_.frame = CGRectZero;
    dimmer_button_.frame = CGRectZero;
    bolder_button_.frame = CGRectZero;
    color_button_.frame = CGRectZero;
    shape_button_.frame = CGRectZero;
    duplicate_button_.frame = CGRectZero;
    undo_button_.frame = CGRectZero;
    redo_button_.frame = CGRectZero;
    add_button_.frame = CGRectZero;
    delete_button_.frame = CGRectZero;
    preview_label_.frame = CGRectZero;
    layout_library_table_.frame = CGRectZero;
    return;
  }

  CGFloat current_y = CGRectGetMaxY(done_button_.frame) + 10.0f;
  selection_label_.frame =
      CGRectMake(kChromeInset, current_y, local_width - kChromeInset * 2.0f, 20.0f);
  current_y = CGRectGetMaxY(selection_label_.frame) + 10.0f;

  if (state_.showing_layout_library) {
    layout_library_table_.frame =
        CGRectMake(kChromeInset, current_y, local_width - kChromeInset * 2.0f,
                   CGRectGetHeight(self.bounds) - current_y - 10.0f);
    undo_button_.frame = CGRectZero;
    redo_button_.frame = CGRectZero;
    preview_label_.frame = CGRectZero;
    return;
  }

  for (NSArray<UIButton*>* row_buttons in [self bodyRows]) {
    current_y = LayoutVisibleButtonsRow(current_y, kChromeInset, local_width - kChromeInset * 2.0f,
                                        kButtonHeight, kRowGap, row_buttons);
  }

  if (!preview_label_.hidden) {
    const CGFloat preview_height =
        TouchEditPreviewHeightForLabel(preview_label_, local_width - kChromeInset * 2.0f);
    preview_label_.frame =
        CGRectMake(kChromeInset, current_y, local_width - kChromeInset * 2.0f, preview_height);
  } else {
    preview_label_.frame = CGRectZero;
  }
  layout_library_table_.frame = CGRectZero;
}

- (UIView*)interactiveHitTestForOverlayPoint:(CGPoint)point
                                       event:(UIEvent*)event
                                      inView:(UIView*)overlayView {
  if (self.hidden || self.alpha <= 0.01f || !self.userInteractionEnabled) {
    return nil;
  }
  for (UIView* subview in [self.subviews reverseObjectEnumerator]) {
    if (subview.hidden || subview.alpha <= 0.01f || !subview.userInteractionEnabled) {
      continue;
    }
    CGPoint local_point = [subview convertPoint:point fromView:overlayView];
    UIView* hit = [subview hitTest:local_point withEvent:event];
    if (hit) {
      return hit;
    }
  }
  CGPoint chrome_point = [self convertPoint:point fromView:overlayView];
  if (!layout_library_table_.hidden &&
      CGRectContainsPoint(layout_library_table_.frame, chrome_point)) {
    return layout_library_table_;
  }
  return nil;
}

- (UIMenu*)bindingMenuForSelectedControl {
  if (!state_.has_selected_control) {
    return nil;
  }
  const auto& control = state_.selected_control;
  const xe::hid::touch::IOSTouchControlType control_type = control.type;
  const xe::hid::touch::IOSTouchAction current_action = control.action;

  std::vector<xe::hid::touch::IOSTouchAction> action_choices;
  if (control_type == xe::hid::touch::IOSTouchControlType::kMoveStick ||
      control_type == xe::hid::touch::IOSTouchControlType::kLookSwipeZone) {
    action_choices = {xe::hid::touch::IOSTouchAction::kMove, xe::hid::touch::IOSTouchAction::kLook};
  } else if (control_type == xe::hid::touch::IOSTouchControlType::kActionButton) {
    action_choices.assign(xe::hid::touch::kIOSTouchEditableActions.begin(),
                          xe::hid::touch::kIOSTouchEditableActions.end());
  } else {
    return nil;
  }

  __unsafe_unretained XeniaTouchOverlayEditChromeIOS* unsafe_self = self;
  NSMutableArray<UIMenuElement*>* children =
      [NSMutableArray arrayWithCapacity:action_choices.size()];
  for (xe::hid::touch::IOSTouchAction action : action_choices) {
    NSString* title =
        [NSString stringWithUTF8String:xe::hid::touch::IOSTouchActionDisplayName(action)];
    UIAction* binding_action =
        [UIAction actionWithTitle:title
                            image:nil
                       identifier:nil
                          handler:^(__unused UIAction* action_handler) {
                            [unsafe_self->delegate_ touchOverlayEditChrome:unsafe_self
                                                          didRequestAction:action];
                          }];
    binding_action.state = action == current_action ? UIMenuElementStateOn : UIMenuElementStateOff;
    [children addObject:binding_action];
  }
  return [UIMenu menuWithTitle:@"" children:children];
}

- (UIMenu*)labelMenuForSelectedControl {
  if (!state_.has_selected_control) {
    return nil;
  }
  const auto& control = state_.selected_control;
  const BOOL label_hidden = control.label_hidden;
  const bool has_custom_label = xe::hid::touch::IOSTouchControlHasCustomLabel(control);
  __unsafe_unretained XeniaTouchOverlayEditChromeIOS* unsafe_self = self;
  NSMutableArray<UIMenuElement*>* children = [NSMutableArray arrayWithCapacity:3];

  UIAction* rename_action = [UIAction
      actionWithTitle:@"Rename…"
                image:nil
           identifier:nil
              handler:^(__unused UIAction* action_handler) {
                [unsafe_self->delegate_ touchOverlayEditChromeDidRequestRenameLabel:unsafe_self];
              }];
  [children addObject:rename_action];

  UIAction* reset_action = [UIAction
      actionWithTitle:@"Use Default Label"
                image:nil
           identifier:nil
              handler:^(__unused UIAction* action_handler) {
                [unsafe_self->delegate_ touchOverlayEditChromeDidRequestResetLabel:unsafe_self];
              }];
  if (!has_custom_label) {
    reset_action.attributes = UIMenuElementAttributesDisabled;
  }
  [children addObject:reset_action];

  UIAction* show_action =
      [UIAction actionWithTitle:@"Show Label"
                          image:nil
                     identifier:nil
                        handler:^(__unused UIAction* action_handler) {
                          [unsafe_self->delegate_ touchOverlayEditChrome:unsafe_self
                                                   didRequestLabelHidden:NO];
                        }];
  show_action.state = label_hidden ? UIMenuElementStateOff : UIMenuElementStateOn;
  [children addObject:show_action];

  UIAction* hide_action =
      [UIAction actionWithTitle:@"Hide Label"
                          image:nil
                     identifier:nil
                        handler:^(__unused UIAction* action_handler) {
                          [unsafe_self->delegate_ touchOverlayEditChrome:unsafe_self
                                                   didRequestLabelHidden:YES];
                        }];
  hide_action.state = label_hidden ? UIMenuElementStateOn : UIMenuElementStateOff;
  [children addObject:hide_action];

  return [UIMenu menuWithTitle:@"" children:children];
}

- (UIMenu*)behaviorMenuForSelectedControl {
  if (!state_.has_selected_control) {
    return nil;
  }
  const auto& control = state_.selected_control;
  const bool is_action_button = control.type == xe::hid::touch::IOSTouchControlType::kActionButton;
  const bool is_move_stick = control.type == xe::hid::touch::IOSTouchControlType::kMoveStick;
  if (!is_action_button && !is_move_stick) {
    return nil;
  }

  const auto& behavior = control.secondary_behavior;
  __unsafe_unretained XeniaTouchOverlayEditChromeIOS* unsafe_self = self;
  NSMutableArray<UIMenuElement*>* children = [NSMutableArray array];

  NSMutableArray<UIMenuElement*>* trigger_children = [NSMutableArray array];
  std::vector<xe::hid::touch::IOSTouchInteractionTrigger> triggers;
  if (is_move_stick) {
    triggers = {xe::hid::touch::IOSTouchInteractionTrigger::kNone,
                xe::hid::touch::IOSTouchInteractionTrigger::kDoubleTapForward};
  } else {
    triggers = {xe::hid::touch::IOSTouchInteractionTrigger::kNone,
                xe::hid::touch::IOSTouchInteractionTrigger::kHold,
                xe::hid::touch::IOSTouchInteractionTrigger::kHoldDrag,
                xe::hid::touch::IOSTouchInteractionTrigger::kDoubleTap};
  }
  for (xe::hid::touch::IOSTouchInteractionTrigger trigger : triggers) {
    NSString* title = [NSString
        stringWithUTF8String:xe::hid::touch::IOSTouchInteractionTriggerDisplayName(trigger)];
    UIAction* trigger_action =
        [UIAction actionWithTitle:title
                            image:nil
                       identifier:nil
                          handler:^(__unused UIAction* action_handler) {
                            [unsafe_self->delegate_ touchOverlayEditChrome:unsafe_self
                                                 didRequestBehaviorTrigger:trigger];
                          }];
    trigger_action.state =
        trigger == behavior.trigger ? UIMenuElementStateOn : UIMenuElementStateOff;
    if (@available(iOS 16.0, *)) {
      trigger_action.attributes = UIMenuElementAttributesKeepsMenuPresented;
    }
    [trigger_children addObject:trigger_action];
  }
  [children addObject:[UIMenu menuWithTitle:@"Trigger" children:trigger_children]];

  NSMutableArray<UIMenuElement*>* output_children = [NSMutableArray array];
  for (xe::hid::touch::IOSTouchAction action : xe::hid::touch::kIOSTouchEditableActions) {
    NSString* title =
        [NSString stringWithUTF8String:xe::hid::touch::IOSTouchActionDisplayName(action)];
    UIAction* output_action =
        [UIAction actionWithTitle:title
                            image:nil
                       identifier:nil
                          handler:^(__unused UIAction* action_handler) {
                            [unsafe_self->delegate_ touchOverlayEditChrome:unsafe_self
                                                  didRequestBehaviorAction:action];
                          }];
    output_action.state = action == behavior.action ? UIMenuElementStateOn : UIMenuElementStateOff;
    [output_children addObject:output_action];
  }
  [children addObject:[UIMenu menuWithTitle:@"Output" children:output_children]];

  return [UIMenu menuWithTitle:@"" children:children];
}

- (UIMenu*)tintMenuForSelectedControl {
  if (!state_.has_selected_control) {
    return nil;
  }
  const xe::hid::touch::IOSTouchTintStyle current_tint = state_.selected_control.tint_style;
  __unsafe_unretained XeniaTouchOverlayEditChromeIOS* unsafe_self = self;
  NSMutableArray<UIMenuElement*>* children =
      [NSMutableArray arrayWithCapacity:xe::hid::touch::kIOSTouchEditableTintStyles.size()];
  for (xe::hid::touch::IOSTouchTintStyle tint_style : xe::hid::touch::kIOSTouchEditableTintStyles) {
    NSString* title =
        [NSString stringWithUTF8String:xe::hid::touch::IOSTouchTintStyleDisplayName(tint_style)];
    UIAction* tint_action =
        [UIAction actionWithTitle:title
                            image:nil
                       identifier:nil
                          handler:^(__unused UIAction* action_handler) {
                            [unsafe_self->delegate_ touchOverlayEditChrome:unsafe_self
                                                       didRequestTintStyle:tint_style];
                          }];
    tint_action.state = tint_style == current_tint ? UIMenuElementStateOn : UIMenuElementStateOff;
    [children addObject:tint_action];
  }
  return [UIMenu menuWithTitle:@"" children:children];
}

- (UIMenu*)lookScaleMenuForSelectedControl {
  if (![self selectedControlSupportsLookScaleTuning] || !state_.has_selected_control) {
    return nil;
  }

  const auto& control = state_.selected_control;
  const bool is_look_zone = control.type == xe::hid::touch::IOSTouchControlType::kLookSwipeZone;
  const float current_scale = [self selectedControlLookScale];
  const float* choices = is_look_zone ? kLookZoneScaleChoices : kRelativeLookScaleChoices;
  const size_t choice_count =
      is_look_zone ? std::size(kLookZoneScaleChoices) : std::size(kRelativeLookScaleChoices);

  __unsafe_unretained XeniaTouchOverlayEditChromeIOS* unsafe_self = self;
  NSMutableArray<UIMenuElement*>* children = [NSMutableArray arrayWithCapacity:choice_count];
  for (size_t index = 0; index < choice_count; ++index) {
    const float scale = choices[index];
    NSString* title = [NSString stringWithFormat:@"%.2fx", scale];
    UIAction* scale_action =
        [UIAction actionWithTitle:title
                            image:nil
                       identifier:nil
                          handler:^(__unused UIAction* action_handler) {
                            [unsafe_self->delegate_ touchOverlayEditChrome:unsafe_self
                                                       didRequestLookScale:scale];
                          }];
    scale_action.state =
        std::abs(scale - current_scale) < 0.001f ? UIMenuElementStateOn : UIMenuElementStateOff;
    if (@available(iOS 16.0, *)) {
      scale_action.attributes = UIMenuElementAttributesKeepsMenuPresented;
    }
    [children addObject:scale_action];
  }
  return [UIMenu menuWithTitle:@"" children:children];
}

- (UIMenu*)shapeMenuForSelectedControl {
  if (!state_.has_selected_control ||
      state_.selected_control.type != xe::hid::touch::IOSTouchControlType::kActionButton) {
    return nil;
  }

  const xe::hid::touch::IOSTouchControlShape current_shape = state_.selected_control.shape;
  __unsafe_unretained XeniaTouchOverlayEditChromeIOS* unsafe_self = self;
  NSMutableArray<UIMenuElement*>* children =
      [NSMutableArray arrayWithCapacity:sizeof(kEditShapeChoices) / sizeof(kEditShapeChoices[0])];
  for (xe::hid::touch::IOSTouchControlShape shape : kEditShapeChoices) {
    UIAction* shape_action = [UIAction
        actionWithTitle:TouchControlShapeDisplayText(shape)
                  image:nil
             identifier:nil
                handler:^(__unused UIAction* action_handler) {
                  [unsafe_self->delegate_ touchOverlayEditChrome:unsafe_self didRequestShape:shape];
                }];
    shape_action.state = shape == current_shape ? UIMenuElementStateOn : UIMenuElementStateOff;
    [children addObject:shape_action];
  }
  return [UIMenu menuWithTitle:@"" children:children];
}

- (UIMenu*)styleMenuForSelectedControl {
  if (!state_.has_selected_control) {
    return nil;
  }

  const auto& control = state_.selected_control;
  __unsafe_unretained XeniaTouchOverlayEditChromeIOS* unsafe_self = self;
  NSMutableArray<UIMenuElement*>* children = [NSMutableArray array];

  UIMenu* tint_menu = [self tintMenuForSelectedControl];
  if (tint_menu.children.count) {
    [children addObject:[UIMenu menuWithTitle:@"Tint" children:tint_menu.children]];
  }

  UIMenu* look_scale_menu = [self lookScaleMenuForSelectedControl];
  if (look_scale_menu.children.count) {
    [children addObject:[UIMenu menuWithTitle:@"Look Sensitivity"
                                     children:look_scale_menu.children]];
  }

  if (control.type == xe::hid::touch::IOSTouchControlType::kActionButton) {
    UIMenu* shape_menu = [self shapeMenuForSelectedControl];
    if (shape_menu.children.count) {
      [children addObject:[UIMenu menuWithTitle:@"Shape" children:shape_menu.children]];
    }
  }

  NSString* opacity_title =
      [NSString stringWithFormat:@"Opacity %.0f%%", control.visual_opacity * 100.0f];
  UIAction* opacity_info = [UIAction actionWithTitle:opacity_title
                                               image:nil
                                          identifier:nil
                                             handler:^(__unused UIAction* action_handler){
                                             }];
  opacity_info.attributes = UIMenuElementAttributesDisabled;
  [children addObject:opacity_info];

  UIAction* dimmer_action = [UIAction
      actionWithTitle:@"Dimmer"
                image:nil
           identifier:nil
              handler:^(__unused UIAction* action_handler) {
                [unsafe_self->delegate_ touchOverlayEditChromeDidRequestDimmerControl:unsafe_self];
              }];
  UIAction* bolder_action = [UIAction
      actionWithTitle:@"Bolder"
                image:nil
           identifier:nil
              handler:^(__unused UIAction* action_handler) {
                [unsafe_self->delegate_ touchOverlayEditChromeDidRequestBolderControl:unsafe_self];
              }];
  if (@available(iOS 16.0, *)) {
    dimmer_action.attributes = UIMenuElementAttributesKeepsMenuPresented;
    bolder_action.attributes = UIMenuElementAttributesKeepsMenuPresented;
  }
  [children addObject:dimmer_action];
  [children addObject:bolder_action];

  return [UIMenu menuWithTitle:@"" children:children];
}

- (UIMenu*)moreMenuForSelectedControl {
  if (!state_.has_selected_control) {
    return nil;
  }

  const auto& control = state_.selected_control;
  const BOOL can_duplicate = control.type == xe::hid::touch::IOSTouchControlType::kActionButton;
  const BOOL can_delete = state_.control_count > 1;
  const BOOL can_match_size = state_.can_match_selected_size;

  __unsafe_unretained XeniaTouchOverlayEditChromeIOS* unsafe_self = self;
  NSMutableArray<UIMenuElement*>* children = [NSMutableArray array];

  UIMenu* label_menu = [self labelMenuForSelectedControl];
  if (label_menu.children.count) {
    [children addObject:[UIMenu menuWithTitle:@"Label" children:label_menu.children]];
  }

  if (can_duplicate) {
    UIAction* duplicate_action =
        [UIAction actionWithTitle:@"Duplicate"
                            image:nil
                       identifier:nil
                          handler:^(__unused UIAction* action_handler) {
                            [unsafe_self->delegate_
                                touchOverlayEditChromeDidRequestDuplicateControl:unsafe_self];
                          }];
    [children addObject:duplicate_action];
  }

  if (can_match_size) {
    UIAction* match_action =
        [UIAction actionWithTitle:@"Match Nearest Size"
                            image:nil
                       identifier:nil
                          handler:^(__unused UIAction* action_handler) {
                            [unsafe_self->delegate_
                                touchOverlayEditChromeDidRequestMatchNearestSize:unsafe_self];
                          }];
    [children addObject:match_action];
  }

  UIAction* mirror_action = [UIAction
      actionWithTitle:@"Mirror Horizontally"
                image:[UIImage systemImageNamed:
                                   @"arrow.left.and.right.righttriangle.left.righttriangle.right"]
           identifier:nil
              handler:^(__unused UIAction* action_handler) {
                [unsafe_self->delegate_ touchOverlayEditChromeDidRequestMirrorControl:unsafe_self];
              }];
  [children addObject:mirror_action];

  NSString* copy_layout_title =
      state_.editing_portrait ? @"Copy Layout from Landscape" : @"Copy Layout from Portrait";
  const BOOL from_landscape = state_.editing_portrait;
  UIAction* copy_layout_action =
      [UIAction actionWithTitle:copy_layout_title
                          image:[UIImage systemImageNamed:@"square.on.square"]
                     identifier:nil
                        handler:^(__unused UIAction* action_handler) {
                          [unsafe_self->delegate_ touchOverlayEditChrome:unsafe_self
                                       didRequestCopyLayoutFromLandscape:from_landscape];
                        }];
  [children addObject:copy_layout_action];

  if (control.type == xe::hid::touch::IOSTouchControlType::kMoveStick) {
    NSString* dpad_title = control.move_with_dpad_ring ? @"Disable D-Pad Ring" : @"Add D-Pad Ring";
    UIAction* dpad_ring_action =
        [UIAction actionWithTitle:dpad_title
                            image:[UIImage systemImageNamed:@"dpad"]
                       identifier:nil
                          handler:^(__unused UIAction* action_handler) {
                            [unsafe_self->delegate_
                                touchOverlayEditChromeDidRequestToggleMoveDpadRing:unsafe_self];
                          }];
    [children addObject:dpad_ring_action];
  }

  UIAction* delete_action = [UIAction
      actionWithTitle:@"Delete"
                image:nil
           identifier:nil
              handler:^(__unused UIAction* action_handler) {
                [unsafe_self->delegate_ touchOverlayEditChromeDidRequestDeleteControl:unsafe_self];
              }];
  delete_action.attributes = UIMenuElementAttributesDestructive;
  if (!can_delete) {
    delete_action.attributes = static_cast<UIMenuElementAttributes>(
        delete_action.attributes | UIMenuElementAttributesDisabled);
  }
  [children addObject:delete_action];

  return [UIMenu menuWithTitle:@"" children:children];
}

- (UIMenu*)addControlMenu {
  if (!state_.editing_enabled) {
    return nil;
  }

  __unsafe_unretained XeniaTouchOverlayEditChromeIOS* unsafe_self = self;
  NSMutableArray<UIMenuElement*>* children = [NSMutableArray array];
  const BOOL can_add_control = state_.control_count < xe::hid::touch::kMaxIOSTouchControls;

  auto append_action = ^(NSString* title, xe::hid::touch::IOSTouchControlType type, BOOL enabled) {
    UIAction* action =
        [UIAction actionWithTitle:title
                            image:nil
                       identifier:nil
                          handler:^(__unused UIAction* action_handler) {
                            [unsafe_self->delegate_ touchOverlayEditChrome:unsafe_self
                                                didRequestAddControlOfType:type];
                          }];
    if (!enabled || !can_add_control) {
      action.attributes = UIMenuElementAttributesDisabled;
    }
    [children addObject:action];
  };

  append_action(@"Action Button", xe::hid::touch::IOSTouchControlType::kActionButton, YES);
  append_action(@"Move Stick", xe::hid::touch::IOSTouchControlType::kMoveStick,
                !state_.layout_contains_move_stick);
  append_action(@"Look Zone", xe::hid::touch::IOSTouchControlType::kLookSwipeZone,
                !state_.layout_contains_look_zone);
  append_action(@"Pause Button", xe::hid::touch::IOSTouchControlType::kPauseButton,
                !state_.layout_contains_pause_button);

  return [UIMenu menuWithTitle:@"" children:children];
}

- (void)refreshPickerMenus {
  ConfigureMenuButton(action_button_, [self bindingMenuForSelectedControl]);
  ConfigureMenuButton(label_button_, nil);
  ConfigureMenuButton(behavior_button_, [self behaviorMenuForSelectedControl]);
  ConfigureMenuButton(color_button_, [self styleMenuForSelectedControl]);
  ConfigureMenuButton(shape_button_, nil);
  ConfigureMenuButton(duplicate_button_, [self moreMenuForSelectedControl]);
  ConfigureMenuButton(add_button_, [self addControlMenu]);
}

- (void)doneEditingButtonPressed:(UIButton*)__unused sender {
  [delegate_ touchOverlayEditChromeDidRequestDoneEditing:self];
}

- (void)smallerButtonPressed:(UIButton*)__unused sender {
  [delegate_ touchOverlayEditChromeDidRequestSmallerControl:self];
}

- (void)matchSizeButtonPressed:(UIButton*)__unused sender {
  [delegate_ touchOverlayEditChromeDidRequestMatchNearestSize:self];
}

- (void)largerButtonPressed:(UIButton*)__unused sender {
  [delegate_ touchOverlayEditChromeDidRequestLargerControl:self];
}

- (void)dimmerButtonPressed:(UIButton*)__unused sender {
  [delegate_ touchOverlayEditChromeDidRequestDimmerControl:self];
}

- (void)bolderButtonPressed:(UIButton*)__unused sender {
  [delegate_ touchOverlayEditChromeDidRequestBolderControl:self];
}

- (void)actionButtonPressed:(UIButton*)__unused sender {
  if (action_button_.showsMenuAsPrimaryAction && action_button_.menu != nil) {
    return;
  }
  [delegate_ touchOverlayEditChromeDidRequestCycleAction:self];
}

- (void)labelButtonPressed:(UIButton*)__unused sender {
  if (label_button_.showsMenuAsPrimaryAction && label_button_.menu != nil) {
    return;
  }
  [delegate_ touchOverlayEditChromeDidRequestRenameLabel:self];
}

- (void)behaviorButtonPressed:(UIButton*)__unused sender {
  if (behavior_button_.showsMenuAsPrimaryAction && behavior_button_.menu != nil) {
    return;
  }
}

- (void)colorButtonPressed:(UIButton*)__unused sender {
  if (color_button_.showsMenuAsPrimaryAction && color_button_.menu != nil) {
    return;
  }
  [delegate_ touchOverlayEditChromeDidRequestCycleTint:self];
}

- (void)shapeButtonPressed:(UIButton*)__unused sender {
  if (shape_button_.showsMenuAsPrimaryAction && shape_button_.menu != nil) {
    return;
  }
  [delegate_ touchOverlayEditChromeDidRequestCycleShape:self];
}

- (void)duplicateButtonPressed:(UIButton*)__unused sender {
  [delegate_ touchOverlayEditChromeDidRequestDuplicateControl:self];
}

- (void)undoButtonPressed:(UIButton*)__unused sender {
  [delegate_ touchOverlayEditChromeDidRequestUndo:self];
}

- (void)redoButtonPressed:(UIButton*)__unused sender {
  [delegate_ touchOverlayEditChromeDidRequestRedo:self];
}

- (void)collapseButtonPressed:(UIButton*)__unused sender {
  [delegate_ touchOverlayEditChromeDidRequestCollapse:self];
}

- (void)gridButtonPressed:(UIButton*)__unused sender {
  [delegate_ touchOverlayEditChromeDidRequestToggleGrid:self];
}

- (void)addButtonPressed:(UIButton*)__unused sender {
  if (add_button_.showsMenuAsPrimaryAction && add_button_.menu != nil) {
    return;
  }
  [delegate_ touchOverlayEditChromeDidRequestAddDefaultControl:self];
}

- (void)deleteButtonPressed:(UIButton*)__unused sender {
  [delegate_ touchOverlayEditChromeDidRequestDeleteControl:self];
}

- (void)layoutLibraryButtonPressed:(UIButton*)__unused sender {
  if (state_.showing_layout_library) {
    [delegate_ touchOverlayEditChromeDidRequestHideLayoutLibrary:self];
    return;
  }
  [delegate_ touchOverlayEditChromeDidRequestLayoutLibrary:self];
}

@end
