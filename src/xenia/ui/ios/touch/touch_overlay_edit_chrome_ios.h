/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_IOS_TOUCH_TOUCH_OVERLAY_EDIT_CHROME_IOS_H_
#define XENIA_UI_IOS_TOUCH_TOUCH_OVERLAY_EDIT_CHROME_IOS_H_

#ifdef __OBJC__

#import <UIKit/UIKit.h>

#include <cstddef>

#include "xenia/hid/touch/touch_layout_ios.h"

@class XeniaTouchLayoutLibraryItem;
@class XeniaTouchOverlayEditChromeIOS;

namespace xe::ui::ios::touch_overlay {

struct TouchOverlayEditChromeState {
  bool editing_enabled = false;
  bool showing_layout_library = false;
  bool minimized = false;
  bool grid_enabled = false;
  bool can_undo = false;
  bool can_redo = false;
  bool editing_portrait = false;
  bool has_selected_control = false;
  bool can_match_selected_size = false;
  bool layout_contains_move_stick = false;
  bool layout_contains_look_zone = false;
  bool layout_contains_pause_button = false;
  size_t control_count = 0;
  xe::hid::touch::IOSTouchControlDefinition selected_control;
};

}  // namespace xe::ui::ios::touch_overlay

@protocol XeniaTouchOverlayEditChromeIOSDelegate <NSObject>

- (void)touchOverlayEditChromeDidRequestDoneEditing:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestSmallerControl:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestMatchNearestSize:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestLargerControl:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestDimmerControl:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestBolderControl:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestCycleAction:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestRenameLabel:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestCycleTint:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestCycleShape:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestDuplicateControl:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestUndo:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestRedo:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestCollapse:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestToggleGrid:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestAddDefaultControl:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestDeleteControl:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestLayoutLibrary:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestHideLayoutLibrary:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestLayoutLibrarySaveCopy:
    (XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestLayoutLibraryRename:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestLayoutLibraryDelete:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestLayoutLibraryImport:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestLayoutLibraryExport:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestLayoutLibraryReset:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)chrome
    didRequestLayoutLibraryLoad:(NSString*)localID;
- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)chrome
              didRequestAction:(xe::hid::touch::IOSTouchAction)action;
- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)chrome
         didRequestLabelHidden:(BOOL)hidden;
- (void)touchOverlayEditChromeDidRequestResetLabel:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)chrome
     didRequestBehaviorTrigger:(xe::hid::touch::IOSTouchInteractionTrigger)trigger;
- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)chrome
      didRequestBehaviorAction:(xe::hid::touch::IOSTouchAction)action;
- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)chrome
           didRequestTintStyle:(xe::hid::touch::IOSTouchTintStyle)tintStyle;
- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)chrome
           didRequestLookScale:(float)lookScale;
- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)chrome
               didRequestShape:(xe::hid::touch::IOSTouchControlShape)shape;
- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)chrome
    didRequestCopyLayoutFromLandscape:(BOOL)fromLandscape;
- (void)touchOverlayEditChromeDidRequestMirrorControl:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChromeDidRequestToggleMoveDpadRing:(XeniaTouchOverlayEditChromeIOS*)chrome;
- (void)touchOverlayEditChrome:(XeniaTouchOverlayEditChromeIOS*)chrome
    didRequestAddControlOfType:(xe::hid::touch::IOSTouchControlType)type;

@end

@interface XeniaTouchOverlayEditChromeIOS : UIView

@property(nonatomic, assign) id<XeniaTouchOverlayEditChromeIOSDelegate> delegate;

- (void)applyState:(const xe::ui::ios::touch_overlay::TouchOverlayEditChromeState&)state;
- (CGFloat)preferredHeightForWidth:(CGFloat)width
                   availableHeight:(CGFloat)availableHeight
                            margin:(CGFloat)margin;
- (UIView*)interactiveHitTestForOverlayPoint:(CGPoint)point
                                       event:(UIEvent*)event
                                      inView:(UIView*)overlayView;
- (void)setLayoutLibraryItems:(NSArray<XeniaTouchLayoutLibraryItem*>*)items
         currentLayoutLocalID:(NSString*)currentLayoutLocalID;

@end

#endif  // __OBJC__

#endif  // XENIA_UI_IOS_TOUCH_TOUCH_OVERLAY_EDIT_CHROME_IOS_H_
