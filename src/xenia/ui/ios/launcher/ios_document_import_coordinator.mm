/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#import "xenia/ui/ios/launcher/ios_document_import_coordinator.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <cstdint>

#include "xenia/base/logging.h"

#import "xenia/ui/ios/launcher/ios_content_management.h"
#import "xenia/ui/ios/launcher/ios_game_library.h"
#import "xenia/ui/ios/shared/ios_view_helpers.h"

namespace {

enum class IOSDocumentImportMode : uint8_t {
  kGameImport = 0,
  kTouchLayoutImport,
};

}  // namespace

@implementation XeniaIOSDocumentImportCoordinator {
  id<XeniaIOSDocumentImportCoordinatorHost> _host;
  IOSDocumentImportMode _mode;
}

- (instancetype)initWithHost:(id<XeniaIOSDocumentImportCoordinatorHost>)host {
  if (!(self = [super init])) {
    return nil;
  }
  _host = host;
  _mode = IOSDocumentImportMode::kGameImport;
  return self;
}

- (void)presentGameImportPicker {
  _mode = IOSDocumentImportMode::kGameImport;
  if ([_host documentImportCoordinatorGameStopInProgress]) {
    [_host documentImportCoordinatorSetStatusText:@"Stopping game... Please wait."];
    return;
  }

  NSArray<UTType*>* content_types = @[
    [UTType typeWithFilenameExtension:@"iso"],
    [UTType typeWithFilenameExtension:@"xex"],
    UTTypeData,
  ];

  UIDocumentPickerViewController* picker =
      [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:content_types];
  picker.delegate = self;
  picker.allowsMultipleSelection = NO;
  picker.shouldShowFileExtensions = YES;
  [[_host documentImportCoordinatorPresenter] presentViewController:picker
                                                           animated:YES
                                                         completion:nil];
  [picker release];
}

- (void)presentTouchLayoutImportPickerFromViewController:(UIViewController*)presenter {
  _mode = IOSDocumentImportMode::kTouchLayoutImport;
  UIDocumentPickerViewController* picker =
      [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeData ]];
  picker.delegate = self;
  picker.allowsMultipleSelection = NO;
  picker.shouldShowFileExtensions = YES;
  [presenter presentViewController:picker animated:YES completion:nil];
  [picker release];
}

- (void)importGameAtURL:(NSURL*)url {
  BOOL access_granted = [url startAccessingSecurityScopedResource];
  XELOGI("iOS: User selected game file: {} (security-scoped: {})", [url.path UTF8String],
         access_granted ? "yes" : "no");

  void (^import_selected_game)(void) = ^{
    NSError* import_error = nil;
    std::filesystem::path imported_path =
        [_host documentImportCoordinatorImportGameAtURL:url error:&import_error];
    if (access_granted) {
      [url stopAccessingSecurityScopedResource];
    }

    if (imported_path.empty()) {
      NSString* message = import_error.localizedDescription ?: @"Failed to import selected game.";
      [_host documentImportCoordinatorPresentAlertWithTitle:@"Import Failed" message:message];
      return;
    }

    [_host documentImportCoordinatorRefreshImportedGames];
    NSString* imported_name = ToNSString(imported_path.filename().string());
    if ([_host documentImportCoordinatorJITAcquired]) {
      [_host documentImportCoordinatorLaunchGameAtPath:imported_path displayName:imported_name];
    } else {
      [_host documentImportCoordinatorSetStatusText:
                 [NSString stringWithFormat:@"Imported %@. Waiting for JIT.", imported_name]];
    }
  };

  const std::filesystem::path selected_path([url.path UTF8String]);
  const BOOL likely_direct_game =
      xe::ui::IsISOPath(selected_path) || xe::ui::IsDefaultXexPath(selected_path);
  IOSSelectedContentPackage package_info;
  const BOOL has_content_package_info =
      xe_read_selected_content_package(selected_path, &package_info, nullptr);
  const BOOL is_launchable_package =
      has_content_package_info && xe::ui::IsIOSLaunchableContentType(package_info.content_type);
  const BOOL should_try_title_update_install =
      [_host documentImportCoordinatorCanInstallTitleUpdates] && !likely_direct_game &&
      !is_launchable_package;
  if (should_try_title_update_install) {
    [_host documentImportCoordinatorSetStatusText:@"Checking content package..."];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
      std::string status;
      bool not_title_update = false;
      bool success = [_host documentImportCoordinatorInstallTitleUpdateAtPath:selected_path
                                                                       status:&status
                                                               notTitleUpdate:&not_title_update];

      dispatch_async(dispatch_get_main_queue(), ^{
        if (success) {
          if (access_granted) {
            [url stopAccessingSecurityScopedResource];
          }
          NSString* message = status.empty() ? @"Installed title update." : ToNSString(status);
          [_host documentImportCoordinatorSetStatusText:message];
          [_host documentImportCoordinatorRefreshImportedGames];
          [_host documentImportCoordinatorPresentAlertWithTitle:@"Title Update Installed"
                                                        message:message];
          return;
        }

        if (!not_title_update) {
          if (access_granted) {
            [url stopAccessingSecurityScopedResource];
          }
          NSString* message =
              status.empty() ? @"Title update installation failed." : ToNSString(status);
          [_host documentImportCoordinatorSetStatusText:message];
          [_host documentImportCoordinatorPresentAlertWithTitle:@"Installation Failed"
                                                        message:message];
          return;
        }

        import_selected_game();
      });
    });
    return;
  }

  import_selected_game();
}

#pragma mark - UIDocumentPickerDelegate

- (void)documentPicker:(UIDocumentPickerViewController* __unused)controller
    didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
  if (urls.count == 0) {
    return;
  }

  NSURL* url = urls[0];
  if (_mode == IOSDocumentImportMode::kTouchLayoutImport) {
    _mode = IOSDocumentImportMode::kGameImport;
    [_host documentImportCoordinatorImportTouchLayoutAtURL:url];
    return;
  }

  [self importGameAtURL:url];
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController* __unused)controller {
  XELOGI("iOS: Document picker cancelled");
  if (_mode == IOSDocumentImportMode::kTouchLayoutImport) {
    _mode = IOSDocumentImportMode::kGameImport;
    [_host documentImportCoordinatorTouchLayoutImportCancelled];
  }
}

@end
