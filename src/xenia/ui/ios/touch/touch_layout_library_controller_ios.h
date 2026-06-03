/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_IOS_TOUCH_LAYOUT_LIBRARY_CONTROLLER_IOS_H_
#define XENIA_UI_IOS_TOUCH_LAYOUT_LIBRARY_CONTROLLER_IOS_H_

#import <UIKit/UIKit.h>

#import "xenia/ui/ios/touch/touch_layout_library_view_ios.h"

@interface XeniaTouchLayoutLibraryTableController
    : NSObject <UITableViewDataSource, UITableViewDelegate>

@property(nonatomic, copy) void (^loadHandler)(NSString* localID);
@property(nonatomic, copy) void (^saveCopyHandler)(void);
@property(nonatomic, copy) void (^renameHandler)(void);
@property(nonatomic, copy) void (^deleteHandler)(void);
@property(nonatomic, copy) void (^importHandler)(void);
@property(nonatomic, copy) void (^exportHandler)(void);
@property(nonatomic, copy) void (^resetHandler)(void);

- (void)setItems:(NSArray<XeniaTouchLayoutLibraryItem*>*)items
    currentLayoutLocalID:(NSString*)currentLayoutLocalID;

@end

#endif  // XENIA_UI_IOS_TOUCH_LAYOUT_LIBRARY_CONTROLLER_IOS_H_
