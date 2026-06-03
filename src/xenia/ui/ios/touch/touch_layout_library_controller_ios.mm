/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#import "xenia/ui/ios/touch/touch_layout_library_controller_ios.h"

#import "xenia/ui/ios/shared/ios_theme.h"
#import "xenia/ui/ios/shared/ios_theme_controls.h"

typedef NS_ENUM(NSInteger, XeniaTouchLayoutLibrarySection) {
  kXeniaTouchLayoutLibrarySectionActions = 0,
  kXeniaTouchLayoutLibrarySectionManage,
  kXeniaTouchLayoutLibrarySectionLayouts,
};

typedef NS_ENUM(NSInteger, XeniaTouchLayoutLibraryActionRow) {
  kXeniaTouchLayoutLibraryActionSaveCopy = 0,
  kXeniaTouchLayoutLibraryActionImport,
  kXeniaTouchLayoutLibraryActionExport,
  kXeniaTouchLayoutLibraryActionReset,
};

typedef NS_ENUM(NSInteger, XeniaTouchLayoutLibraryManageRow) {
  kXeniaTouchLayoutLibraryManageRename = 0,
  kXeniaTouchLayoutLibraryManageDelete,
};

@implementation XeniaTouchLayoutLibraryTableController {
  NSArray<XeniaTouchLayoutLibraryItem*>* items_;
  NSString* current_layout_local_id_;
}

@synthesize loadHandler = loadHandler_;
@synthesize saveCopyHandler = saveCopyHandler_;
@synthesize renameHandler = renameHandler_;
@synthesize deleteHandler = deleteHandler_;
@synthesize importHandler = importHandler_;
@synthesize exportHandler = exportHandler_;
@synthesize resetHandler = resetHandler_;

- (void)dealloc {
  [resetHandler_ release];
  [exportHandler_ release];
  [importHandler_ release];
  [deleteHandler_ release];
  [renameHandler_ release];
  [saveCopyHandler_ release];
  [loadHandler_ release];
  [current_layout_local_id_ release];
  [items_ release];
  [super dealloc];
}

- (void)setItems:(NSArray<XeniaTouchLayoutLibraryItem*>*)items
    currentLayoutLocalID:(NSString*)currentLayoutLocalID {
  [items_ release];
  items_ = [items copy];
  [current_layout_local_id_ release];
  current_layout_local_id_ = [currentLayoutLocalID copy];
}

- (BOOL)hasSavedLayoutLibraryItems {
  for (XeniaTouchLayoutLibraryItem* item in items_) {
    if (!item.official) {
      return YES;
    }
  }
  return NO;
}

- (NSInteger)numberOfSectionsInTableView:(UITableView* __unused)tableView {
  return 3;
}

- (NSInteger)tableView:(UITableView* __unused)tableView numberOfRowsInSection:(NSInteger)section {
  switch (section) {
    case kXeniaTouchLayoutLibrarySectionActions:
      return 4;
    case kXeniaTouchLayoutLibrarySectionManage:
      return 2;
    case kXeniaTouchLayoutLibrarySectionLayouts:
      return static_cast<NSInteger>(items_.count);
    default:
      return 0;
  }
}

- (NSString*)tableView:(UITableView* __unused)tableView titleForHeaderInSection:(NSInteger)section {
  switch (section) {
    case kXeniaTouchLayoutLibrarySectionActions:
      return @"Actions";
    case kXeniaTouchLayoutLibrarySectionManage:
      return @"Saved Layouts";
    case kXeniaTouchLayoutLibrarySectionLayouts:
      return @"Browse";
    default:
      return nil;
  }
}

- (NSString*)tableView:(UITableView* __unused)tableView titleForFooterInSection:(NSInteger)section {
  (void)section;
  return nil;
}

- (CGFloat)tableView:(UITableView* __unused)tableView
    heightForHeaderInSection:(NSInteger)__unused section {
  return 24.0f;
}

- (CGFloat)tableView:(UITableView* __unused)tableView
    heightForFooterInSection:(NSInteger)__unused section {
  return 0.01f;
}

- (UIView*)tableView:(UITableView*)tableView viewForHeaderInSection:(NSInteger)section {
  NSString* title = [self tableView:tableView titleForHeaderInSection:section];
  if (!title.length) {
    return nil;
  }

  UIView* container = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
  container.backgroundColor = [UIColor clearColor];

  UILabel* label = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
  label.translatesAutoresizingMaskIntoConstraints = NO;
  label.text = title;
  label.textColor = [[UIColor whiteColor] colorWithAlphaComponent:0.56];
  xe_apply_label_font(label, UIFontTextStyleCaption1, 12.0,
                      UIFontWeightSemibold);
  [container addSubview:label];

  [NSLayoutConstraint activateConstraints:@[
    [label.leadingAnchor constraintEqualToAnchor:container.leadingAnchor constant:18.0f],
    [label.trailingAnchor constraintEqualToAnchor:container.trailingAnchor constant:-18.0f],
    [label.bottomAnchor constraintEqualToAnchor:container.bottomAnchor constant:-4.0f],
  ]];

  return container;
}

- (UITableViewCell*)tableView:(UITableView*)tableView
        cellForRowAtIndexPath:(NSIndexPath*)indexPath {
  static NSString* const kTouchLayoutLibraryCellIdentifier = @"XeniaTouchLayoutLibraryCell";
  XeniaTouchLayoutLibraryRowCell* cell =
      [tableView dequeueReusableCellWithIdentifier:kTouchLayoutLibraryCellIdentifier];
  if (!cell) {
    cell = [[[XeniaTouchLayoutLibraryRowCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                                  reuseIdentifier:kTouchLayoutLibraryCellIdentifier]
        autorelease];
  }

  cell.overlayTitleLabel.textColor = [[UIColor whiteColor] colorWithAlphaComponent:0.96];
  cell.overlaySubtitleLabel.textColor = [[UIColor whiteColor] colorWithAlphaComponent:0.64];
  cell.overlayTitleLabel.text = @"";
  cell.overlaySubtitleLabel.text = @"";
  cell.selectionStyle = UITableViewCellSelectionStyleDefault;
  [cell setShowsDisclosure:NO];
  [cell setShowsCheckmark:NO];
  [cell setThumbnailImage:nil];
  [cell setShowsDefaultBadge:NO];

  switch (indexPath.section) {
    case kXeniaTouchLayoutLibrarySectionActions:
      switch (indexPath.row) {
        case kXeniaTouchLayoutLibraryActionSaveCopy:
          cell.overlayTitleLabel.text = @"Save Layout Copy";
          cell.overlaySubtitleLabel.text = @"Create a reusable local copy from the current layout.";
          break;
        case kXeniaTouchLayoutLibraryActionImport:
          cell.overlayTitleLabel.text = @"Import Layout File";
          cell.overlaySubtitleLabel.text =
              @"Load a `.touchlayout.toml` file into the current title.";
          break;
        case kXeniaTouchLayoutLibraryActionExport:
          cell.overlayTitleLabel.text = @"Export Current Layout";
          cell.overlaySubtitleLabel.text = @"Share the current title-local touch layout as a file.";
          break;
        case kXeniaTouchLayoutLibraryActionReset:
          cell.overlayTitleLabel.text = @"Reset to Official Preset";
          cell.overlaySubtitleLabel.text =
              @"Restore the current title to its official base preset.";
          // "Reset" is a destructive-but-recoverable action — warning amber
          // rather than danger red.
          cell.overlayTitleLabel.textColor =
              [[XeniaTheme statusWarning] colorWithAlphaComponent:0.98];
          break;
        default:
          break;
      }
      [cell setShowsDisclosure:YES];
      break;

    case kXeniaTouchLayoutLibrarySectionManage: {
      const BOOL enabled = [self hasSavedLayoutLibraryItems];
      cell.overlayTitleLabel.text = indexPath.row == kXeniaTouchLayoutLibraryManageRename
                                        ? @"Rename Saved Layout"
                                        : @"Delete Saved Layout";
      cell.overlaySubtitleLabel.text = indexPath.row == kXeniaTouchLayoutLibraryManageRename
                                           ? @"Rename one of your saved local layout copies."
                                           : @"Delete one of your saved local layout copies.";
      cell.selectionStyle =
          enabled ? UITableViewCellSelectionStyleDefault : UITableViewCellSelectionStyleNone;
      [cell setShowsDisclosure:enabled];
      if (!enabled) {
        cell.overlayTitleLabel.textColor =
            [[UIColor whiteColor] colorWithAlphaComponent:[XeniaTheme opacityHeavy]];
        cell.overlaySubtitleLabel.textColor =
            [[UIColor whiteColor] colorWithAlphaComponent:[XeniaTheme opacityStrong]];
      } else if (indexPath.row == kXeniaTouchLayoutLibraryManageDelete) {
        cell.overlayTitleLabel.textColor =
            [[XeniaTheme statusError] colorWithAlphaComponent:0.98];
      }
      break;
    }

    case kXeniaTouchLayoutLibrarySectionLayouts: {
      if (indexPath.row >= 0 && indexPath.row < static_cast<NSInteger>(items_.count)) {
        XeniaTouchLayoutLibraryItem* item = [items_ objectAtIndex:indexPath.row];
        cell.overlayTitleLabel.text = item.displayName ?: item.localID;
        NSMutableArray<NSString*>* subtitle_parts = [NSMutableArray array];
        [subtitle_parts addObject:(item.official ? @"Official preset" : @"Saved local copy")];
        if (item.author.length > 0) {
          [subtitle_parts addObject:item.author];
        }
        if (item.isDefaultForCurrentTitle) {
          [subtitle_parts addObject:@"Default for this game"];
        }
        cell.overlaySubtitleLabel.text = [subtitle_parts componentsJoinedByString:@" • "];
        [cell setShowsCheckmark:[item.localID isEqualToString:current_layout_local_id_]];
        [cell setThumbnailImage:item.thumbnail];
        [cell setShowsDefaultBadge:item.isDefaultForCurrentTitle];
      }
      break;
    }
    default:
      break;
  }

  return cell;
}

- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)indexPath {
  [tableView deselectRowAtIndexPath:indexPath animated:YES];

  switch (indexPath.section) {
    case kXeniaTouchLayoutLibrarySectionActions:
      switch (indexPath.row) {
        case kXeniaTouchLayoutLibraryActionSaveCopy:
          if (saveCopyHandler_) {
            saveCopyHandler_();
          }
          break;
        case kXeniaTouchLayoutLibraryActionImport:
          if (importHandler_) {
            importHandler_();
          }
          break;
        case kXeniaTouchLayoutLibraryActionExport:
          if (exportHandler_) {
            exportHandler_();
          }
          break;
        case kXeniaTouchLayoutLibraryActionReset:
          if (resetHandler_) {
            resetHandler_();
          }
          break;
        default:
          break;
      }
      break;

    case kXeniaTouchLayoutLibrarySectionManage:
      if (![self hasSavedLayoutLibraryItems]) {
        return;
      }
      if (indexPath.row == kXeniaTouchLayoutLibraryManageRename) {
        if (renameHandler_) {
          renameHandler_();
        }
      } else if (indexPath.row == kXeniaTouchLayoutLibraryManageDelete) {
        if (deleteHandler_) {
          deleteHandler_();
        }
      }
      break;

    case kXeniaTouchLayoutLibrarySectionLayouts:
      if (indexPath.row < 0 || indexPath.row >= static_cast<NSInteger>(items_.count)) {
        return;
      }
      if (loadHandler_) {
        XeniaTouchLayoutLibraryItem* item = [items_ objectAtIndex:indexPath.row];
        [current_layout_local_id_ release];
        current_layout_local_id_ = [item.localID copy];
        loadHandler_(item.localID);
        [tableView reloadData];
      }
      break;

    default:
      break;
  }
}

@end
