/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_TOUCH_CONTROLS_IOS_H_
#define XENIA_UI_TOUCH_CONTROLS_IOS_H_

#ifdef __OBJC__
#import <UIKit/UIKit.h>
#include "xenia/hid/input.h"

NS_ASSUME_NONNULL_BEGIN

@interface XeniaTouchControlsView : UIView
@property(nonatomic, copy, nullable) void (^stateDidChangeHandler)(void);
@property(nonatomic, copy, nullable) void (^menuButtonTappedHandler)(void);
- (xe::hid::X_INPUT_STATE)currentControllerState;
- (void)resetControllerState;
@end

NS_ASSUME_NONNULL_END
#endif  // __OBJC__

#endif  // XENIA_UI_TOUCH_CONTROLS_IOS_H_
