/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// iOS entry point. Each piece of the launcher / settings / compatibility UI
// lives in its own ios_*.{h,mm} pair under src/xenia/ui/; this file is just
// the UIApplicationMain trampoline that hands control to XeniaAppDelegate.

#import <UIKit/UIKit.h>

#import "xenia/ui/ios/app/ios_app_delegate.h"

int main(int argc, char* argv[]) {
  NSLog(@"iOS: skipping ptrace/debugger JIT setup; using normal W^X app flow.");
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil, NSStringFromClass([XeniaAppDelegate class]));
  }
}
