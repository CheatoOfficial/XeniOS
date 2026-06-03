/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#import "xenia/ui/ios/app/ios_main_view_controller.h"

#import <GameController/GameController.h>
#import <PhotosUI/PhotosUI.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/config.h"
#include "xenia/hid/input.h"
#include "xenia/vfs/stfs_metadata.h"
#include "xenia/xbox.h"

#import "xenia/hid/touch/touch_layout_ios.h"
#import "xenia/ui/ios/app/ios_controller_navigation_coordinator.h"
#import "xenia/ui/ios/app/ios_in_game_menu_overlay.h"
#import "xenia/ui/ios/app/ios_landscape_navigation_controller.h"
#import "xenia/ui/ios/app/ios_window_layout.h"
#import "xenia/ui/ios/app/ios_window_position_overlay.h"
#import "xenia/ui/ios/app/windowed_app_context_ios.h"
#import "xenia/ui/ios/game/ios_achievement_notification_presenter.h"
#import "xenia/ui/ios/game/ios_achievements_view_controller.h"
#import "xenia/ui/ios/game/ios_metal_view.h"
#import "xenia/ui/ios/launcher/ios_compat_data.h"
#import "xenia/ui/ios/launcher/ios_compat_report_view_controller.h"
#import "xenia/ui/ios/launcher/ios_content_management.h"
#import "xenia/ui/ios/launcher/ios_document_import_coordinator.h"
#import "xenia/ui/ios/launcher/ios_external_url.h"
#import "xenia/ui/ios/launcher/ios_game_art.h"
#import "xenia/ui/ios/launcher/ios_game_actions_view_controller.h"
#import "xenia/ui/ios/launcher/ios_game_compatibility_view_controller.h"
#import "xenia/ui/ios/launcher/ios_game_content_view_controller.h"
#import "xenia/ui/ios/launcher/ios_game_disc_view_controller.h"
#import "xenia/ui/ios/launcher/ios_game_library.h"
#import "xenia/ui/ios/launcher/ios_game_library_store.h"
#import "xenia/ui/ios/launcher/ios_game_patches_view_controller.h"
#import "xenia/ui/ios/launcher/ios_game_picker_view_controller.h"
#import "xenia/ui/ios/launcher/ios_game_tile_cell.h"
#import "xenia/ui/ios/launcher/ios_launcher_overlay_view.h"
#import "xenia/ui/ios/settings/ios_choice_list_view_controller.h"
#import "xenia/ui/ios/settings/ios_config_builder.h"
#import "xenia/ui/ios/settings/ios_config_models.h"
#import "xenia/ui/ios/settings/ios_config_view_controller.h"
#import "xenia/ui/ios/settings/ios_quick_settings_view_controller.h"
#import "xenia/ui/ios/settings/ios_log_view_controller.h"
#import "xenia/ui/ios/settings/ios_profile_view_controller.h"
#import "xenia/ui/ios/settings/ios_settings_hub_view_controller.h"
#import "xenia/ui/ios/shared/ios_status_toast.h"
#import "xenia/ui/ios/shared/ios_system_utils.h"
#import "xenia/ui/ios/shared/ios_theme.h"
#import "xenia/ui/ios/shared/ios_view_helpers.h"
#import "xenia/ui/ios/touch/touch_controls_overlay_ios.h"
#import "xenia/ui/ios/touch/touch_layout_editor_view_controller_ios.h"
#import "xenia/ui/ios/touch/touch_layout_library_ios.h"
#import "xenia/ui/ios/touch/touch_layout_store_ios.h"
#import "xenia/ui/ios/touch/touch_layout_ui_coordinator_ios.h"

DECLARE_bool(ios_touch_overlay);
DECLARE_bool(present_letterbox);

using xe::ui::ApplyIOSCompatibilityDataToDiscoveredGames;
using xe::ui::BuildDiscoveredGameFromPath;
using xe::ui::ExtractLaunchPathFromExternalURL;
using xe::ui::ExtractLaunchTitleIDFromExternalURL;
using xe::ui::FormatTitleID;
using xe::ui::ImportGameIntoIOSLibrary;
using xe::ui::IOSDiscoveredGame;
using xe::ui::IOSImportedGamesDirectory;
using xe::ui::IsExternalGameInfoRequestURL;
using xe::ui::IsLikelyGodContainerFile;
using xe::ui::NormalizeURLToken;
using xe::ui::ScanIOSGameLibrary;
using xe::ui::SortDiscoveredGames;
using xe::ui::xe_game_info_callback_provider;
using xe::ui::xe_launch_url_for_title_id;
using xe::ui::xe_stikdebug_enable_jit_url_for_bundle_identifier;

namespace {

constexpr NSTimeInterval kXeniaAutoStikDebugCooldownSeconds = 10.0;

NSTimeInterval GetUnixTimeSeconds() { return [[NSDate date] timeIntervalSince1970]; }

std::string TrimAscii(std::string value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(start, end - start);
}

static BOOL xe_game_system_supports_compatibility(xe::ui::IOSGameSystem system) {
  return system == xe::ui::IOSGameSystem::kXbox360;
}

static BOOL xe_game_system_supports_manage_content(xe::ui::IOSGameSystem system) {
  return system == xe::ui::IOSGameSystem::kXbox360;
}

static BOOL xe_game_system_supports_remote_art(xe::ui::IOSGameSystem system) {
  return system == xe::ui::IOSGameSystem::kXbox360;
}

}  // namespace

// Private touch-overlay/library methods and internal UI state. Kept out of the
// public header so the app delegate sees only the bridge surface it needs.
@interface XeniaViewController () <XeniaIOSControllerNavigationHost,
                                   XeniaIOSDocumentImportCoordinatorHost,
                                   XeniaIOSTouchLayoutUICoordinatorHost,
                                   XeniaGameContentHost,
                                   UIAdaptivePresentationControllerDelegate>
@property(nonatomic, strong) XeniaIOSLauncherOverlayView* launcherOverlayView;
@property(nonatomic, strong) UILabel* signedInProfileLabel;
@property(nonatomic, strong) XeniaIOSInGameMenuOverlay* inGameMenuOverlay;
@property(nonatomic, strong) XeniaIOSWindowPositionOverlay* windowPositionOverlay;
@property(nonatomic, strong)
    XeniaIOSControllerNavigationCoordinator* controllerNavigationCoordinator;
@property(nonatomic, strong) XeniaIOSDocumentImportCoordinator* documentImportCoordinator;
@property(nonatomic, strong) XeniaIOSTouchLayoutUICoordinator* touchLayoutCoordinator;
@property(nonatomic, strong)
    XeniaIOSAchievementNotificationPresenter* achievementNotificationPresenter;
@property(nonatomic, strong) XeniaIOSStatusToastPresenter* statusToastPresenter;
@property(nonatomic, strong) XeniaTouchControlsOverlayView* touchControlsOverlay;
@property(nonatomic, assign) BOOL gameRunning;
@property(nonatomic, assign) BOOL gameStopInProgress;
@property(nonatomic, strong) NSTimer* jitPollTimer;
@property(nonatomic, assign) NSTimeInterval jitPollInterval;
@property(nonatomic, assign) NSTimeInterval jitPollStartedAt;
@property(nonatomic, assign) BOOL jitPollTimedOut;
@property(nonatomic, assign) BOOL jitAcquired;
- (BOOL)hasConnectedGameplayController;
- (BOOL)shouldBlockGameplayInput;
- (BOOL)shouldShowTouchControlsOverlay;
- (void)refreshLauncherGameSnapshots;
- (void)refreshImportedGamesAsync;
- (void)finishImportedGamesRefresh;
- (void)setTouchLayoutEditModeActive:(BOOL)active animated:(BOOL)animated;
- (void)finishTouchLayoutEditMode;
- (void)updateTouchControlsOverlayVisibilityAnimated:(BOOL)animated;
- (void)showStatusToast:(NSString*)message style:(XeniaIOSStatusToastStyle)style;
- (void)showStatusToastForMessage:(NSString*)message;
- (void)presentGameActionsSheetForIndex:(size_t)game_index;
- (void)presentTouchLayoutGamePicker;
- (void)performGameAction:(XeniaIOSGameAction)action forIndex:(size_t)game_index;
- (void)presentGameSettingsSheetForIndex:(size_t)game_index;
- (void)presentGameTouchLayoutSheetForIndex:(size_t)game_index;
- (void)presentCompatibilitySheetForIndex:(size_t)game_index;
- (void)presentManageContentSheetForIndex:(size_t)game_index;
- (void)presentDiscSelectionSheetForIndex:(size_t)game_index;
- (void)presentPatchesSheetForIndex:(size_t)game_index;
- (void)scheduleNextJITPoll;
- (void)pauseButtonTapped:(id)sender;
- (void)resumeGameTapped:(UIButton*)sender;
- (void)inGameEditControlsTapped:(UIButton*)sender;
- (void)inGameAchievementsTapped:(UIButton*)sender;
- (void)inGameSettingsTapped:(UIButton*)sender;
- (void)inGameLiveLogTapped:(UIButton*)sender;
- (void)exitGameTapped:(UIButton*)sender;
- (void)handleSettingsHubAction:(XeniaSettingsHubAction)action;
- (void)refreshInGameDisplayMenu;
- (void)applyDefaultTouchLayoutModel;
- (void)applyTouchLayoutModelForTitleID:(uint32_t)title_id;
- (void)presentTouchLayoutLibrarySheet;
- (void)presentRenameTouchLayoutSheet;
- (void)presentDeleteTouchLayoutSheet;
- (void)saveCurrentTouchLayoutCopy;
- (UIViewController*)topPresentedControllerForModalPresentation;
- (void)importTouchLayoutFromFile;
- (void)exportCurrentTouchLayout;
- (void)resetToOfficialTouchLayoutPreset;
- (void)saveTouchLayoutModelForTitleID:(uint32_t)title_id;
- (uint32_t)titleIDForGamePath:(const std::filesystem::path&)game_path;
- (BOOL)handleExternalTouchLayoutFileURL:(NSURL*)url;
- (BOOL)handleExternalTouchLayoutSchemeURL:(NSURL*)url;
- (void)presentPendingTouchLayoutInstallIfReady;
- (void)handleAppDidBecomeActive:(NSNotification*)note;
- (void)handleControllerConnectionChanged:(NSNotification*)note;
- (BOOL)isGuestDisplayUncapped;
- (void)setGuestDisplayUncapped:(BOOL)uncapped;
- (BOOL)isPresentLetterboxEnabled;
- (void)setPresentLetterboxEnabled:(BOOL)enabled;

// Presents `rootController` as the standardized in-game/settings sheet: wraps it
// in a XeniaLandscapeNavigationController, applies the shared task-sheet chrome,
// suppresses the gameplay touch overlay while presented, and clears that
// suppression on both completion and interactive dismissal. Centralizes the
// boilerplate previously duplicated across the quick settings, live log and
// achievements presenters. Returns the autoreleased wrapping controller.
- (XeniaLandscapeNavigationController*)
    presentPauseSheetWithRootController:(UIViewController*)rootController
                          preferredSize:(CGSize)preferredSize
            preventInteractiveDismissal:(BOOL)preventInteractiveDismissal
                             completion:(void (^)(void))completion;
@end

@implementation XeniaViewController {
  std::vector<IOSDiscoveredGame> discovered_games_;
  NSDictionary* compat_data_;
  CGSize last_collection_layout_size_;
  uint64_t library_refresh_generation_;
  BOOL compat_fetch_started_;
  std::filesystem::path pending_external_launch_path_;
  std::unique_ptr<xe::hid::touch::IOSTouchRuntimeModel> touch_runtime_model_;
  BOOL touch_layout_edit_mode_active_;
  BOOL gameplay_modal_presentation_pending_;
  uint32_t active_game_title_id_;
  std::string active_touch_layout_local_id_;
  UITapGestureRecognizer* in_game_menu_tap_recognizer_;  // owned by self.view
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = [UIColor blackColor];
  self.jitAcquired = NO;
  self.gameRunning = NO;
  self.gameStopInProgress = NO;
  last_collection_layout_size_ = CGSizeZero;
  compat_fetch_started_ = NO;
  touch_layout_edit_mode_active_ = NO;
  gameplay_modal_presentation_pending_ = NO;
  active_game_title_id_ = 0;
  active_touch_layout_local_id_.clear();
  XeniaIOSControllerNavigationCoordinator* controller_navigation =
      [[XeniaIOSControllerNavigationCoordinator alloc] initWithHost:self];
  self.controllerNavigationCoordinator = controller_navigation;
  [controller_navigation release];
  XeniaIOSDocumentImportCoordinator* document_import =
      [[XeniaIOSDocumentImportCoordinator alloc] initWithHost:self];
  self.documentImportCoordinator = document_import;
  [document_import release];
  XeniaIOSTouchLayoutUICoordinator* touch_layout =
      [[XeniaIOSTouchLayoutUICoordinator alloc] initWithHost:self];
  self.touchLayoutCoordinator = touch_layout;
  [touch_layout release];
  XeniaIOSAchievementNotificationPresenter* achievement_notifications =
      [[XeniaIOSAchievementNotificationPresenter alloc] init];
  self.achievementNotificationPresenter = achievement_notifications;
  [achievement_notifications release];
  XeniaIOSStatusToastPresenter* status_toasts = [[XeniaIOSStatusToastPresenter alloc] init];
  self.statusToastPresenter = status_toasts;
  [status_toasts release];
  // Create the Metal-backed rendering view. The frame is set explicitly via
  // -applyMetalViewLayout in -viewDidLayoutSubviews so the user-chosen
  // window scaling mode (Fit / Stretch / Zoom) and portrait position offset
  // can be applied. autoresizingMask is intentionally NOT set — we manage
  // the frame on every layout pass instead. clipsToBounds keeps the Zoom
  // mode (which may extend the metal view past the parent in one dimension
  // to maintain aspect) from leaking outside our view controller's view.
  self.view.clipsToBounds = YES;
  self.metalView = [[XeniaMetalView alloc] initWithFrame:self.view.bounds];
  self.metalView.contentScaleFactor = [UIScreen mainScreen].scale;
  [self.view addSubview:self.metalView];

  // Create the iOS touch overlay above the Metal view. Hidden by default;
  // visibility is driven by -updateTouchControlsOverlayVisibilityAnimated:.
  touch_runtime_model_ = std::make_unique<xe::hid::touch::IOSTouchRuntimeModel>();
  XeniaTouchControlsOverlayView* touch_overlay =
      [[XeniaTouchControlsOverlayView alloc] initWithRuntimeModel:touch_runtime_model_.get()];
  touch_overlay.frame = self.view.bounds;
  touch_overlay.autoresizingMask =
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  self.touchControlsOverlay = touch_overlay;
  __block XeniaViewController* block_self = self;
  self.touchControlsOverlay.pauseHandler = ^{
    [block_self pauseButtonTapped:nil];
  };
  self.touchControlsOverlay.doneEditingHandler = ^{
    [block_self finishTouchLayoutEditMode];
  };
  self.touchControlsOverlay.layoutLibraryHandler = ^{
    [block_self presentTouchLayoutLibrarySheet];
  };
  self.touchControlsOverlay.layoutLibraryLoadHandler = ^(NSString* local_id) {
    [block_self.touchLayoutCoordinator applyLayoutWithLocalID:local_id];
  };
  self.touchControlsOverlay.layoutLibrarySaveCopyHandler = ^{
    [block_self saveCurrentTouchLayoutCopy];
  };
  self.touchControlsOverlay.layoutLibraryRenameHandler = ^{
    [block_self presentRenameTouchLayoutSheet];
  };
  self.touchControlsOverlay.layoutLibraryDeleteHandler = ^{
    [block_self presentDeleteTouchLayoutSheet];
  };
  self.touchControlsOverlay.layoutLibraryImportHandler = ^{
    [block_self importTouchLayoutFromFile];
  };
  self.touchControlsOverlay.layoutLibraryExportHandler = ^{
    [block_self exportCurrentTouchLayout];
  };
  self.touchControlsOverlay.layoutLibraryResetHandler = ^{
    [block_self resetToOfficialTouchLayoutPreset];
  };
  [self.view addSubview:self.touchControlsOverlay];
  [touch_overlay release];

  // Create the launcher overlay UI immediately. When JIT is missing, keep
  // settings/navigation available but gate game launch with status.
  [self setupLauncherOverlay];
  [self setupInGameMenuOverlay];
  UITapGestureRecognizer* tap =
      [[UITapGestureRecognizer alloc] initWithTarget:self
                                              action:@selector(toggleInGameMenuTapped:)];
  tap.numberOfTapsRequired = 1;
  tap.cancelsTouchesInView = NO;
  [self.view addGestureRecognizer:tap];
  in_game_menu_tap_recognizer_ = tap;
  [tap release];
  [self updateJITStatusIndicator];
  [self updateJITAvailabilityUI];
  [self refreshSignedInProfileUI];
  NSDictionary* cached_compat_data = xe_load_cached_compat_data();
  if (cached_compat_data) {
    [compat_data_ release];
    compat_data_ = [cached_compat_data retain];
  }
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(onCompatDataDidUpdate:)
                                               name:kXeniaCompatDataDidUpdateNotification
                                             object:nil];
  [self refreshImportedGames];

  // Start polling for JIT.
  [self startJITPoll];
  [self.controllerNavigationCoordinator start];

  // Cold-launch "Open in XeniOS" with auto-StikDebug enabled backgrounds the
  // app right after the URL is dispatched, killing any in-flight modal.
  // Listening for the foreground transition lets us present the install
  // confirm once we're back on screen.
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(handleAppDidBecomeActive:)
                                               name:UIApplicationDidBecomeActiveNotification
                                             object:nil];
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(handleControllerConnectionChanged:)
                                               name:GCControllerDidConnectNotification
                                             object:nil];
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(handleControllerConnectionChanged:)
                                               name:GCControllerDidDisconnectNotification
                                             object:nil];
}

- (void)handleAppDidBecomeActive:(NSNotification* __unused)note {
  if (!self.jitAcquired && !self.jitPollTimer) {
    [self startJITPoll];
  }
  if (!self.gameRunning && self.launcherOverlayView.hidden == NO) {
    [self refreshImportedGamesAsync];
  }
  [self presentPendingTouchLayoutInstallIfReady];
}

- (void)handleControllerConnectionChanged:(NSNotification* __unused)note {
  [self.controllerNavigationCoordinator refreshLauncherFocus];
  [self.controllerNavigationCoordinator refreshInGameFocus];
  [self updateTouchControlsOverlayVisibilityAnimated:YES];
}

- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
  [self startCompatFetchIfNeeded];
  xe_request_current_orientation(self);
  [self presentPendingTouchLayoutInstallIfReady];
}

- (void)startCompatFetchIfNeeded {
  if (compat_fetch_started_) {
    return;
  }
  compat_fetch_started_ = YES;
  xe_fetch_compat_data(^(NSDictionary* data) {
    if (!data) {
      return;
    }
    if (self->compat_data_ && [self->compat_data_ isEqualToDictionary:data]) {
      return;
    }
    std::filesystem::path focused_path;
    const NSInteger focused_game_index = self.controllerNavigationCoordinator.focusedGameIndex;
    const BOOL has_focused_path =
        focused_game_index >= 0 &&
        focused_game_index < static_cast<NSInteger>(self->discovered_games_.size());
    if (has_focused_path) {
      focused_path = self->discovered_games_[static_cast<size_t>(focused_game_index)].path;
    }
    [self->compat_data_ release];
    self->compat_data_ = [data retain];
    [self applyCompatDataToDiscoveredGames];
    SortDiscoveredGames(&self->discovered_games_);
    if (has_focused_path) {
      auto focused_it = std::find_if(
          self->discovered_games_.begin(), self->discovered_games_.end(),
          [&focused_path](const IOSDiscoveredGame& game) { return game.path == focused_path; });
      if (focused_it != self->discovered_games_.end()) {
        [self.controllerNavigationCoordinator
            setFocusedGameIndex:static_cast<NSInteger>(
                                    std::distance(self->discovered_games_.begin(), focused_it))
                         scroll:NO];
      }
    }
    [self refreshLauncherGameSnapshots];
    [self.controllerNavigationCoordinator refreshLauncherFocus];
  });
}

- (void)onCompatDataDidUpdate:(NSNotification*)__unused notification {
  NSDictionary* cached_compat_data = xe_load_cached_compat_data();
  if (!cached_compat_data) {
    return;
  }
  std::filesystem::path focused_path;
  const NSInteger focused_game_index = self.controllerNavigationCoordinator.focusedGameIndex;
  const BOOL has_focused_path =
      focused_game_index >= 0 &&
      focused_game_index < static_cast<NSInteger>(discovered_games_.size());
  if (has_focused_path) {
    focused_path = discovered_games_[static_cast<size_t>(focused_game_index)].path;
  }
  [compat_data_ release];
  compat_data_ = [cached_compat_data retain];
  [self applyCompatDataToDiscoveredGames];
  SortDiscoveredGames(&discovered_games_);
  if (has_focused_path) {
    auto focused_it = std::find_if(
        discovered_games_.begin(), discovered_games_.end(),
        [&focused_path](const IOSDiscoveredGame& game) { return game.path == focused_path; });
    if (focused_it != discovered_games_.end()) {
      [self.controllerNavigationCoordinator
          setFocusedGameIndex:static_cast<NSInteger>(
                                  std::distance(discovered_games_.begin(), focused_it))
                       scroll:NO];
    }
  }
  [self refreshLauncherGameSnapshots];
  [self.controllerNavigationCoordinator refreshLauncherFocus];
}

#pragma mark - Controller navigation host

- (BOOL)controllerNavigationLauncherVisible {
  return self.launcherOverlayView && !self.launcherOverlayView.hidden;
}

- (BOOL)controllerNavigationLauncherActionsEnabled {
  return self.launcherOverlayView.actionsEnabled;
}

- (NSInteger)controllerNavigationGameCount {
  return static_cast<NSInteger>(discovered_games_.size());
}

- (NSInteger)controllerNavigationLauncherColumnCount {
  return [self.launcherOverlayView columnCount];
}

- (NSInteger)controllerNavigationLauncherPageStep {
  return [self.launcherOverlayView pageStep];
}

- (BOOL)controllerNavigationGameRunning {
  return self.gameRunning;
}

- (BOOL)controllerNavigationInGameMenuVisible {
  return self.inGameMenuOverlay && !self.inGameMenuOverlay.hidden;
}

- (BOOL)controllerNavigationInGameMenuActionEnabled:(XeniaIOSInGameMenuAction)action {
  return [self.inGameMenuOverlay isActionEnabled:action];
}

- (UIViewController*)controllerNavigationPresentedController {
  return self.presentedViewController;
}

- (BOOL)controllerNavigationHasConnectedController {
  return [self hasConnectedGameplayController];
}

- (BOOL)controllerNavigationReadEmulatorControllerState:(xe::hid::X_INPUT_STATE*)outState {
  if (!self.appContext || !outState) {
    return NO;
  }
  for (uint32_t user_index = 0; user_index < xe::XUserMaxUserCount; ++user_index) {
    if (self.appContext->GetControllerState(user_index, outState)) {
      return YES;
    }
  }
  return NO;
}

- (void)controllerNavigationApplyFocusedGameIndex:(NSInteger)index scroll:(BOOL)scroll {
  [self.launcherOverlayView setFocusedGameIndex:index scroll:scroll];
}

- (void)controllerNavigationApplyLauncherFocusEnabled:(BOOL)enabled
                                      settingsFocused:(BOOL)settingsFocused
                                       profileFocused:(BOOL)profileFocused
                                        importFocused:(BOOL)importFocused
                                   libraryFocusActive:(BOOL)libraryFocusActive {
  [self.launcherOverlayView setControllerNavigationEnabled:enabled
                                           settingsFocused:settingsFocused
                                            profileFocused:profileFocused
                                             importFocused:importFocused
                                        libraryFocusActive:libraryFocusActive];
}

- (void)controllerNavigationApplyInGameMenuFocusEnabled:(BOOL)enabled
                                          focusedAction:(XeniaIOSInGameMenuAction)focusedAction {
  [self.inGameMenuOverlay setControllerNavigationEnabled:enabled focusedAction:focusedAction];
}

- (void)controllerNavigationOpenSettings {
  [self openSettingsTapped:nil];
}

- (void)controllerNavigationOpenProfile {
  [self openProfileTapped:nil];
}

- (void)controllerNavigationImportGame {
  [self openGameTapped:nil];
}

- (void)controllerNavigationManageGameAtIndex:(NSInteger)index {
  if (index < 0 || index >= static_cast<NSInteger>(discovered_games_.size())) {
    return;
  }
  [self presentGameActionsSheetForIndex:static_cast<size_t>(index)];
}

- (void)controllerNavigationLaunchGameAtIndex:(NSInteger)index {
  if (index < 0 || index >= static_cast<NSInteger>(discovered_games_.size())) {
    return;
  }
  const IOSDiscoveredGame& game = discovered_games_[static_cast<size_t>(index)];
  [self launchGameAtPath:game.path displayName:ToNSString(game.title)];
}

- (void)controllerNavigationShowInGameMenu {
  [self showInGameMenuOverlayAnimated:YES];
}

- (void)controllerNavigationHideInGameMenu {
  [self hideInGameMenuOverlay];
}

- (void)controllerNavigationPerformInGameMenuAction:(XeniaIOSInGameMenuAction)action {
  [self.inGameMenuOverlay performAction:action];
}

// ---------------------------------------------------------------------------
// JIT polling -- starts responsive, then backs off to avoid burning battery
// forever when StikDebug / JIT is unavailable.
// ---------------------------------------------------------------------------
- (void)startJITPoll {
  // Check immediately first.
  if (xe_check_jit_available()) {
    [self onJITAcquired];
    return;
  }

  XELOGI("iOS: JIT not yet available, polling...");
  self.jitPollInterval = 0.5;
  self.jitPollStartedAt = [NSDate timeIntervalSinceReferenceDate];
  self.jitPollTimedOut = NO;
  [self scheduleNextJITPoll];
}

- (void)pollJIT:(NSTimer*)timer {
  if (timer != self.jitPollTimer) {
    return;
  }
  self.jitPollTimer = nil;
  if (xe_check_jit_available()) {
    [self onJITAcquired];
    return;
  }

  const NSTimeInterval elapsed = [NSDate timeIntervalSinceReferenceDate] - self.jitPollStartedAt;
  if (elapsed >= 300.0) {
    self.jitPollTimedOut = YES;
    [self updateJITStatusIndicator];
    NSString* message = @"JIT is still unavailable. Open StikDebug, then return here to retry.";
    [self showStatusToast:message style:XeniaIOSStatusToastStyleWarning];
    return;
  }

  self.jitPollInterval = std::min<NSTimeInterval>(self.jitPollInterval * 2.0, 10.0);
  [self scheduleNextJITPoll];
}

- (void)scheduleNextJITPoll {
  [self.jitPollTimer invalidate];
  self.jitPollTimer = [NSTimer scheduledTimerWithTimeInterval:self.jitPollInterval
                                                       target:self
                                                     selector:@selector(pollJIT:)
                                                     userInfo:nil
                                                      repeats:NO];
}

- (void)onJITAcquired {
  [self.jitPollTimer invalidate];
  self.jitPollTimer = nil;
  self.jitPollTimedOut = NO;
  self.jitAcquired = YES;
  XELOGI("iOS: JIT acquired!");
  [self updateJITStatusIndicator];
  [self updateJITAvailabilityUI];

  std::filesystem::path queued_path = pending_external_launch_path_;
  std::filesystem::path persisted_path = TakePendingExternalLaunchPathPreference();
  pending_external_launch_path_.clear();

  if (!queued_path.empty() || !persisted_path.empty()) {
    std::filesystem::path path_to_launch = !queued_path.empty() ? queued_path : persisted_path;
    NSString* display_name = [self displayNameForGamePath:path_to_launch];
    if (!display_name || display_name.length == 0) {
      display_name = ToNSString(path_to_launch.filename().string());
    }
    XELOGI("iOS: Launching queued external request: {}", path_to_launch.string());
    [self launchGameAtPath:path_to_launch displayName:display_name];
  }
}

// ---------------------------------------------------------------------------
// Launcher overlay — delegated to XeniaIOSLauncherOverlayView.
// ---------------------------------------------------------------------------
- (void)setupLauncherOverlay {
  XeniaIOSLauncherOverlayView* launcher_overlay =
      [[XeniaIOSLauncherOverlayView alloc] initWithFrame:self.view.bounds];
  self.launcherOverlayView = launcher_overlay;
  [launcher_overlay release];
  [self.view addSubview:self.launcherOverlayView];

  __unsafe_unretained XeniaViewController* unsafe_self = self;
  self.launcherOverlayView.settingsHandler = ^{
    [unsafe_self openSettingsTapped:nil];
  };
  self.launcherOverlayView.profileHandler = ^{
    [unsafe_self openProfileTapped:nil];
  };
  self.launcherOverlayView.importHandler = ^{
    [unsafe_self openGameTapped:nil];
  };
  self.launcherOverlayView.gameLaunchedHandler = ^(NSUInteger gameIndex) {
    if (gameIndex >= unsafe_self->discovered_games_.size()) {
      return;
    }
    const IOSDiscoveredGame& game = unsafe_self->discovered_games_[gameIndex];
    [unsafe_self launchGameAtPath:game.path displayName:ToNSString(game.title)];
  };
  self.launcherOverlayView.compatibilityHandler = ^(NSUInteger gameIndex) {
    [unsafe_self presentCompatibilitySheetForIndex:gameIndex];
  };
  self.launcherOverlayView.gameSettingsHandler = ^(NSUInteger gameIndex) {
    [unsafe_self presentGameSettingsSheetForIndex:gameIndex];
  };
  self.launcherOverlayView.touchLayoutHandler = ^(NSUInteger gameIndex) {
    [unsafe_self presentGameTouchLayoutSheetForIndex:gameIndex];
  };
  self.launcherOverlayView.manageContentHandler = ^(NSUInteger gameIndex) {
    [unsafe_self presentManageContentSheetForIndex:gameIndex];
  };
  self.launcherOverlayView.discSelectionHandler = ^(NSUInteger gameIndex) {
    [unsafe_self presentDiscSelectionSheetForIndex:gameIndex];
  };
  self.launcherOverlayView.patchesHandler = ^(NSUInteger gameIndex) {
    [unsafe_self presentPatchesSheetForIndex:gameIndex];
  };
  self.launcherOverlayView.copyLaunchURLHandler = ^(NSUInteger gameIndex) {
    [unsafe_self copyLaunchURLForGameAtIndex:gameIndex];
  };

  // Route the public statusLabel to the overlay's internal label.
  self.statusLabel = self.launcherOverlayView.statusLabel;

  // Allocated but off-screen — existing code can set .text without crashing.
  UILabel* signed_in_profile_label = [[UILabel alloc] init];
  self.signedInProfileLabel = signed_in_profile_label;
  [signed_in_profile_label release];
}

- (void)refreshLauncherGameSnapshots {
  NSMutableArray<XeniaIOSLauncherGameSnapshot*>* snapshots =
      [NSMutableArray arrayWithCapacity:discovered_games_.size()];
  for (const IOSDiscoveredGame& game : discovered_games_) {
    XeniaIOSLauncherGameSnapshot* snapshot = [[XeniaIOSLauncherGameSnapshot alloc] init];
    snapshot.title =
        game.title.empty() ? ToNSString(game.path.stem().string()) : ToNSString(game.title);
    snapshot.titleId = game.title_id;
    snapshot.hasCompatInfo = game.has_compat_info;
    snapshot.compatStatus = ToNSString(game.compat_status);
    if (!game.content_type_name.empty() &&
        (game.content_type == xe::XContentType::kArcadeTitle ||
         game.content_type == xe::XContentType::kGameDemo ||
         game.content_type == xe::XContentType::kCommunityGame)) {
      snapshot.contentTypeName = ToNSString(game.content_type_name);
    } else if (game.discs.size() > 1) {
      snapshot.contentTypeName = [NSString stringWithFormat:@"%zu Discs", game.discs.size()];
    }
    snapshot.supportsCompatibility =
        game.title_id != 0 && xe_game_system_supports_compatibility(game.system);
    snapshot.supportsManageContent =
        game.title_id != 0 && xe_game_system_supports_manage_content(game.system);
    snapshot.supportsDiscSelection = game.discs.size() > 1;
    snapshot.discCount = game.discs.size();
    snapshot.supportsPatches = self.appContext && game.title_id != 0;
    snapshot.supportsRemoteArt = xe_game_system_supports_remote_art(game.system);
    if (!game.icon_data.empty()) {
      snapshot.iconData = [NSData dataWithBytes:game.icon_data.data() length:game.icon_data.size()];
    }
    [snapshots addObject:snapshot];
    [snapshot release];
  }
  [self.launcherOverlayView setGames:snapshots];
  [self.launcherOverlayView
      setFocusedGameIndex:self.controllerNavigationCoordinator.focusedGameIndex
                   scroll:NO];
}

- (void)setupInGameMenuOverlay {
  XeniaIOSInGameMenuOverlay* overlay =
      [[XeniaIOSInGameMenuOverlay alloc] initWithFrame:self.view.bounds];
  self.inGameMenuOverlay = overlay;
  [overlay release];
  [self.view addSubview:self.inGameMenuOverlay];

  __unsafe_unretained XeniaViewController* unsafe_self = self;
  self.inGameMenuOverlay.resumeHandler = ^{
    [unsafe_self resumeGameTapped:nil];
  };
  self.inGameMenuOverlay.editControlsHandler = ^{
    [unsafe_self inGameEditControlsTapped:nil];
  };
  self.inGameMenuOverlay.achievementsHandler = ^{
    [unsafe_self inGameAchievementsTapped:nil];
  };
  self.inGameMenuOverlay.settingsHandler = ^{
    [unsafe_self inGameSettingsTapped:nil];
  };
  self.inGameMenuOverlay.liveLogHandler = ^{
    [unsafe_self inGameLiveLogTapped:nil];
  };
  self.inGameMenuOverlay.exitHandler = ^{
    [unsafe_self exitGameTapped:nil];
  };
  self.inGameMenuOverlay.graphicsHandler = ^{
    [unsafe_self presentQuickSettings];
  };
  [self refreshInGameDisplayMenu];
}

- (void)refreshInGameDisplayMenu {
  self.inGameMenuOverlay.displayMenu = [self buildInGameDisplayMenu];
}

- (void)toggleInGameMenuTapped:(UITapGestureRecognizer*)recognizer {
  if (recognizer.state != UIGestureRecognizerStateRecognized) {
    return;
  }
  if (self.launcherOverlayView.hidden == NO || !self.gameRunning || self.presentedViewController) {
    return;
  }

  // If the in-game menu is showing AND the tap landed on one of its buttons,
  // do NOT toggle the menu — let the button handle its own action. Without
  // this guard, opening the Display button's UIMenu (or any other menu-button
  // in the panel) would also dismiss the surrounding in-game menu overlay,
  // leaving the user with a floating UIMenu over empty space and forcing
  // them to tap-to-reopen afterwards. Tap recognizer was originally added
  // with cancelsTouchesInView=NO so the button still receives the tap.
  if (!self.inGameMenuOverlay.hidden) {
    CGPoint tap_in_overlay = [recognizer locationInView:self.inGameMenuOverlay];
    UIView* hit = [self.inGameMenuOverlay hitTest:tap_in_overlay withEvent:nil];
    if (hit && hit != self.inGameMenuOverlay) {
      return;
    }
  }

  if (self.inGameMenuOverlay.hidden) {
    [self showInGameMenuOverlayAnimated:YES];
  } else {
    [self hideInGameMenuOverlay];
  }
}

- (void)showInGameMenuOverlayAnimated:(BOOL)animated {
  if (!self.inGameMenuOverlay.hidden) {
    return;
  }

  if (touch_layout_edit_mode_active_) {
    [self saveTouchLayoutModelForTitleID:active_game_title_id_];
    [self setTouchLayoutEditModeActive:NO animated:NO];
  }

  [self refreshInGameDisplayMenu];
  [self.inGameMenuOverlay setOverlayVisible:YES animated:animated completion:nil];
  [self.controllerNavigationCoordinator focusDefaultInGameAction];
  [self updateTouchControlsOverlayVisibilityAnimated:NO];
}

- (void)hideInGameMenuOverlay {
  if (self.inGameMenuOverlay.hidden) {
    return;
  }
  [self.inGameMenuOverlay setOverlayVisible:NO
                                   animated:YES
                                 completion:^(__unused BOOL finished) {
                                   [self.controllerNavigationCoordinator refreshInGameFocus];
                                   [self updateTouchControlsOverlayVisibilityAnimated:YES];
                                 }];
}

- (void)resumeGameTapped:(UIButton*)sender {
  [self hideInGameMenuOverlay];
}

- (void)inGameEditControlsTapped:(UIButton*)sender {
  [self hideInGameMenuOverlay];
  [self setTouchLayoutEditModeActive:YES animated:YES];
}

- (void)inGameAchievementsTapped:(UIButton*)sender {
  [self hideInGameMenuOverlay];
  [self presentAchievementsForUserIndex:0 titleID:active_game_title_id_ completion:nil];
}

- (void)inGameSettingsTapped:(UIButton*)sender {
  [self hideInGameMenuOverlay];
  [self openSettingsTapped:nil];
}

- (XeniaLandscapeNavigationController*)
    presentPauseSheetWithRootController:(UIViewController*)rootController
                          preferredSize:(CGSize)preferredSize
            preventInteractiveDismissal:(BOOL)preventInteractiveDismissal
                             completion:(void (^)(void))completion {
  if (self.gameRunning) {
    gameplay_modal_presentation_pending_ = YES;
    [self updateTouchControlsOverlayVisibilityAnimated:YES];
  }
  XeniaLandscapeNavigationController* nav =
      [[XeniaLandscapeNavigationController alloc] initWithRootViewController:rootController];
  XEConfigureTaskSheet(nav, self.view, preferredSize, preventInteractiveDismissal);
  nav.presentationController.delegate = self;
  UIViewController* presenter = [self topPresentedControllerForModalPresentation] ?: self;
  [presenter presentViewController:nav
                          animated:YES
                        completion:^{
                          gameplay_modal_presentation_pending_ = NO;
                          [self updateTouchControlsOverlayVisibilityAnimated:YES];
                          if (completion) {
                            completion();
                          }
                        }];
  return [nav autorelease];
}

- (void)presentQuickSettings {
  [self hideInGameMenuOverlay];
  XeniaIOSQuickSettingsViewController* quickVC =
      [[XeniaIOSQuickSettingsViewController alloc] init];
  [self presentPauseSheetWithRootController:quickVC
                             preferredSize:CGSizeMake(480.0, 560.0)
               preventInteractiveDismissal:NO
                                completion:nil];
  [quickVC release];
}

- (void)inGameLiveLogTapped:(UIButton*)sender {
  [self hideInGameMenuOverlay];
  XeniaLogViewController* log_vc = [[XeniaLogViewController alloc] init];
  [self presentPauseSheetWithRootController:log_vc
                             preferredSize:CGSizeMake(560.0, 620.0)
               preventInteractiveDismissal:NO
                                completion:nil];
  [log_vc release];
}

- (void)exitGameTapped:(UIButton*)sender {
  [self hideInGameMenuOverlay];
  if (self.gameStopInProgress) {
    self.statusLabel.text = @"";
    [self showStatusToast:@"Stopping game... Please wait." style:XeniaIOSStatusToastStyleInfo];
    return;
  }
  if (!self.appContext) {
    return;
  }

  self.gameStopInProgress = YES;
  self.gameRunning = NO;
  self.launcherOverlayView.hidden = NO;
  self.launcherOverlayView.alpha = 1.0;
  [self updateTouchControlsOverlayVisibilityAnimated:YES];
  xe_request_current_orientation(self);
  self.statusLabel.text = @"";
  [self showStatusToast:@"Stopping game..." style:XeniaIOSStatusToastStyleInfo];

  xe::ui::IOSWindowedAppContext* app_context = self.appContext;
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    BOOL requested_stop = app_context->TerminateCurrentGame() ? YES : NO;
    if (requested_stop) {
      return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      self.gameStopInProgress = NO;
    });
  });
}

- (void)refreshSignedInProfileUI {
  if (!self.appContext) {
    self.launcherOverlayView.actionsEnabled = NO;
    self.launcherOverlayView.alpha = 0.5;
    self.signedInProfileLabel.text = @"Profile system unavailable";
    return;
  }

  self.launcherOverlayView.actionsEnabled = YES;
  self.launcherOverlayView.alpha = 1.0;

  const auto profiles = self.appContext->ListProfiles();
  const xe::ui::IOSProfileSummary* signed_in_profile = nullptr;
  for (const auto& profile : profiles) {
    if (profile.signed_in) {
      signed_in_profile = &profile;
      break;
    }
  }

  if (signed_in_profile) {
    self.signedInProfileLabel.text =
        [NSString stringWithFormat:@"Signed in: %@", ToNSString(signed_in_profile->gamertag)];
  } else if (profiles.empty()) {
    self.signedInProfileLabel.text = @"No local profile yet";
  } else {
    self.signedInProfileLabel.text = @"No profile signed in";
  }
}

- (void)presentProfileCreateAlert {
  // Under MRC, `__weak` is unavailable; rely on block strong captures.
  // Use `__block` for the alert to avoid a retain-cycle: alert -> action -> handler -> alert.
  __block UIAlertController* create_alert =
      [UIAlertController alertControllerWithTitle:@"Create Profile"
                                          message:@"Enter a gamertag (1-15 characters)."
                                   preferredStyle:UIAlertControllerStyleAlert];
  [create_alert addTextFieldWithConfigurationHandler:^(UITextField* text_field) {
    text_field.placeholder = @"Gamertag";
    text_field.autocapitalizationType = UITextAutocapitalizationTypeWords;
    text_field.autocorrectionType = UITextAutocorrectionTypeNo;
    text_field.clearButtonMode = UITextFieldViewModeWhileEditing;
  }];
  [create_alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                                   style:UIAlertActionStyleCancel
                                                 handler:nil]];
  [create_alert
      addAction:[UIAlertAction
                    actionWithTitle:@"Create"
                              style:UIAlertActionStyleDefault
                            handler:^(__unused UIAlertAction* action) {
                              if (!self.appContext) {
                                return;
                              }
                              UITextField* text_field = create_alert.textFields.firstObject;
                              NSString* raw_text = text_field.text ?: @"";
                              NSString* trimmed =
                                  [raw_text stringByTrimmingCharactersInSet:
                                                [NSCharacterSet whitespaceAndNewlineCharacterSet]];
                              if (trimmed.length == 0 || !self.appContext) {
                                return;
                              }
                              auto* app_context = self.appContext;
                              if (!app_context) {
                                return;
                              }
                              NSString* gamertag = [[trimmed copy] autorelease];
                              create_alert = nil;
                              uint64_t xuid =
                                  app_context->CreateProfile(std::string([gamertag UTF8String]));
                              if (!xuid) {
                                UIAlertController* failure = [UIAlertController
                                    alertControllerWithTitle:@"Profile Not Created"
                                                     message:@"Profile could not be created. "
                                                             @"Please try again."
                                              preferredStyle:UIAlertControllerStyleAlert];
                                [failure addAction:[UIAlertAction
                                                       actionWithTitle:@"OK"
                                                                 style:UIAlertActionStyleCancel
                                                               handler:nil]];
                                [self presentViewController:failure animated:YES completion:nil];
                                return;
                              }
                              BOOL signed_in = app_context->SignInProfile(xuid);
                              if (!signed_in) {
                                [self showStatusToast:@"Failed to sign in with the new profile."
                                                style:XeniaIOSStatusToastStyleError];
                                return;
                              }
                              [self refreshSignedInProfileUI];
                              [self showStatusToast:[NSString stringWithFormat:@"Signed in as %@.",
                                                                               gamertag]
                                              style:XeniaIOSStatusToastStyleSuccess];
                            }]];
  [self presentViewController:create_alert animated:YES completion:nil];
}

- (void)presentSettingsDestinationWithInitialSection:(XeniaSettingsInitialSection)initial_section
                         preventInteractiveDismissal:(BOOL)prevent_interactive_dismissal {
  if (!self.appContext) {
    return;
  }

  if (self.gameRunning) {
    gameplay_modal_presentation_pending_ = YES;
    [self updateTouchControlsOverlayVisibilityAnimated:YES];
  }

  __block XeniaViewController* unsafe_self = self;
  XeniaSettingsStatusHandler status_handler = ^(NSString* status_message) {
    [unsafe_self refreshSignedInProfileUI];
    if (status_message.length > 0) {
      [unsafe_self showStatusToastForMessage:status_message];
    }
  };
  void (^dismissal_handler)(void) = ^{
    gameplay_modal_presentation_pending_ = NO;
    [unsafe_self updateTouchControlsOverlayVisibilityAnimated:YES];
  };
  XeniaSettingsHubActionHandler action_handler = ^(XeniaSettingsHubAction action) {
    [unsafe_self handleSettingsHubAction:action];
  };

  const BOOL use_workspace =
      UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad &&
      self.traitCollection.horizontalSizeClass != UIUserInterfaceSizeClassCompact;

  UIViewController* destination = nil;
  if (use_workspace) {
    XeniaSettingsWorkspaceViewController* workspace =
        [[XeniaSettingsWorkspaceViewController alloc] initWithAppContext:self.appContext
                                                                onStatus:status_handler
                                                          initialSection:initial_section];
    workspace.dismissalHandler = dismissal_handler;
    workspace.actionHandler = action_handler;
    destination = workspace;
  } else {
    XeniaSettingsHubViewController* hub =
        [[XeniaSettingsHubViewController alloc] initWithAppContext:self.appContext
                                                          onStatus:status_handler
                                                    initialSection:initial_section];
    hub.showsCloseButton = YES;
    hub.dismissalHandler = dismissal_handler;
    hub.actionHandler = action_handler;
    XeniaLandscapeNavigationController* nav =
        [[XeniaLandscapeNavigationController alloc] initWithRootViewController:hub];
    [hub release];
    destination = nav;
  }

  XEConfigureDestinationControllerPresentation(
      destination, self.view, use_workspace ? CGSizeMake(920.0, 760.0) : CGSizeMake(620.0, 760.0),
      prevent_interactive_dismissal);
  destination.presentationController.delegate = self;

  UIViewController* presenter = [self topPresentedControllerForModalPresentation] ?: self;
  [presenter presentViewController:destination
                          animated:YES
                        completion:^{
                          gameplay_modal_presentation_pending_ = NO;
                          [self updateTouchControlsOverlayVisibilityAnimated:YES];
                        }];
  [destination release];
}

- (void)openProfileTapped:(UIButton*)sender {
  (void)sender;
  if (!self.appContext) {
    return;
  }

  if (self.gameRunning) {
    gameplay_modal_presentation_pending_ = YES;
    [self updateTouchControlsOverlayVisibilityAnimated:YES];
  }

  __block XeniaViewController* unsafe_self = self;
  IOSProfileStatusHandler status_handler = ^(NSString* status_message) {
    [unsafe_self refreshSignedInProfileUI];
    if (status_message.length > 0) {
      [unsafe_self showStatusToastForMessage:status_message];
    }
  };

  XeniaProfileViewController* profile =
      [[XeniaProfileViewController alloc] initWithAppContext:self.appContext
                                                    onStatus:status_handler];
  profile.showsDismissButton = YES;
  XeniaLandscapeNavigationController* navigation_controller =
      [[XeniaLandscapeNavigationController alloc] initWithRootViewController:profile];
  XEConfigureDestinationPresentation(navigation_controller, self.view, CGSizeMake(520.0, 680.0),
                                     YES);
  navigation_controller.presentationController.delegate = self;

  UIViewController* presenter = [self topPresentedControllerForModalPresentation] ?: self;
  [presenter presentViewController:navigation_controller
                          animated:YES
                        completion:^{
                          gameplay_modal_presentation_pending_ = NO;
                          [unsafe_self updateTouchControlsOverlayVisibilityAnimated:YES];
                        }];
  [navigation_controller release];
  [profile release];
}

- (void)dismissPresentedDestinationForSettingsAction:(void (^)(void))completion {
  void (^finish)(void) = ^{
    gameplay_modal_presentation_pending_ = NO;
    [self updateTouchControlsOverlayVisibilityAnimated:YES];
    if (completion) {
      completion();
    }
  };

  if (self.presentedViewController) {
    [self dismissViewControllerAnimated:YES completion:finish];
  } else {
    finish();
  }
}

- (void)handleSettingsHubAction:(XeniaSettingsHubAction)action {
  switch (action) {
    case XeniaSettingsHubActionOpenTouchLayoutLibrary:
      [self presentTouchLayoutLibrarySheet];
      break;
    case XeniaSettingsHubActionChooseGameTouchLayout: {
      __block XeniaViewController* unsafe_self = self;
      [self dismissPresentedDestinationForSettingsAction:^{
        [unsafe_self presentTouchLayoutGamePicker];
      }];
    } break;
    case XeniaSettingsHubActionEditTouchControls: {
      if (!self.gameRunning) {
        XEPresentOKAlert([self topPresentedControllerForModalPresentation] ?: self,
                         @"Game Required",
                         @"Launch a game before editing the running touch layout.");
        return;
      }
      __block XeniaViewController* unsafe_self = self;
      [self dismissPresentedDestinationForSettingsAction:^{
        [unsafe_self inGameEditControlsTapped:nil];
      }];
    } break;
    case XeniaSettingsHubActionImportTouchLayout:
      [self importTouchLayoutFromFile];
      break;
    case XeniaSettingsHubActionExportTouchLayout:
      [self exportCurrentTouchLayout];
      break;
    case XeniaSettingsHubActionResetTouchLayout:
      [self resetToOfficialTouchLayoutPreset];
      break;
    case XeniaSettingsHubActionImportGame:
      [self.documentImportCoordinator presentGameImportPicker];
      break;
    case XeniaSettingsHubActionRefreshLibrary:
      [self refreshImportedGames];
      break;
    case XeniaSettingsHubActionNone:
    default:
      break;
  }
}

- (void)presentSystemSigninPromptForUserIndex:(uint32_t)user_index
                                  usersNeeded:(uint32_t)users_needed
                                   completion:(void (^)(BOOL success))completion {
  if (!self.appContext) {
    if (completion) {
      completion(NO);
    }
    return;
  }

  __block BOOL finished = NO;
  void (^finish)(BOOL) = ^(BOOL success) {
    if (finished) {
      return;
    }
    finished = YES;
    [self refreshSignedInProfileUI];
    if (completion) {
      completion(success);
    }
  };

  auto profiles = self.appContext->ListProfiles();
  void (^present_create_alert)(void) = ^{
    if (!self.appContext) {
      finish(NO);
      return;
    }

    // Under MRC, use `__block` to avoid a retain-cycle: alert -> action -> handler -> alert.
    __block UIAlertController* create_alert =
        [UIAlertController alertControllerWithTitle:@"Create Profile"
                                            message:@"Enter a gamertag (1-15 characters)."
                                     preferredStyle:UIAlertControllerStyleAlert];
    [create_alert addTextFieldWithConfigurationHandler:^(UITextField* text_field) {
      text_field.placeholder = @"Gamertag";
      text_field.autocapitalizationType = UITextAutocapitalizationTypeWords;
      text_field.autocorrectionType = UITextAutocorrectionTypeNo;
      text_field.clearButtonMode = UITextFieldViewModeWhileEditing;
    }];
    [create_alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                                     style:UIAlertActionStyleCancel
                                                   handler:^(__unused UIAlertAction* action) {
                                                     finish(NO);
                                                   }]];
    [create_alert
        addAction:
            [UIAlertAction
                actionWithTitle:@"Create"
                          style:UIAlertActionStyleDefault
                        handler:^(__unused UIAlertAction* action) {
                          UITextField* text_field = create_alert.textFields.firstObject;
                          NSString* raw_text = text_field.text ?: @"";
                          NSString* trimmed =
                              [raw_text stringByTrimmingCharactersInSet:
                                            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
                          if (trimmed.length == 0 || !self.appContext) {
                            finish(NO);
                            return;
                          }
                          auto* app_context = self.appContext;
                          if (!app_context) {
                            finish(NO);
                            return;
                          }
                          NSString* gamertag = [[trimmed copy] autorelease];
                          create_alert = nil;
                          uint64_t xuid =
                              app_context->CreateProfile(std::string([gamertag UTF8String]));
                          if (!xuid) {
                            [self showStatusToast:@"Profile could not be created. Please try again."
                                            style:XeniaIOSStatusToastStyleError];
                            finish(NO);
                            return;
                          }
                          BOOL signed_in = app_context->SignInProfile(xuid);
                          if (signed_in) {
                            [self showStatusToast:[NSString stringWithFormat:@"Signed in as %@.",
                                                                             gamertag]
                                            style:XeniaIOSStatusToastStyleSuccess];
                          } else {
                            [self showStatusToast:@"Failed to sign in with the new profile."
                                            style:XeniaIOSStatusToastStyleError];
                          }
                          finish(signed_in);
                        }]];

    UIViewController* presenter = self;
    while (presenter.presentedViewController) {
      presenter = presenter.presentedViewController;
    }
    [presenter presentViewController:create_alert animated:YES completion:nil];
  };

  if (profiles.empty()) {
    present_create_alert();
    return;
  }

  NSString* message = [NSString stringWithFormat:@"Select profile (needs %u user%@).", users_needed,
                                                 users_needed == 1 ? @"" : @"s"];
  UIAlertController* sheet =
      [UIAlertController alertControllerWithTitle:@"Select Profile"
                                          message:message
                                   preferredStyle:UIAlertControllerStyleActionSheet];

  [sheet addAction:[UIAlertAction actionWithTitle:@"Create Profile"
                                            style:UIAlertActionStyleDefault
                                          handler:^(__unused UIAlertAction* action) {
                                            present_create_alert();
                                          }]];

  for (const auto& profile : profiles) {
    NSString* gamertag = ToNSString(profile.gamertag);
    NSString* title = gamertag;
    if (profile.signed_in) {
      title = [title stringByAppendingString:@" (Signed In)"];
    }
    uint64_t xuid = profile.xuid;
    [sheet addAction:[UIAlertAction actionWithTitle:title
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction* action) {
                                              if (!self.appContext) {
                                                finish(NO);
                                                return;
                                              }
                                              BOOL signed_in = self.appContext->SignInProfile(xuid);
                                              finish(signed_in);
                                            }]];
  }

  [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                            style:UIAlertActionStyleCancel
                                          handler:^(__unused UIAlertAction* action) {
                                            finish(NO);
                                          }]];

  UIPopoverPresentationController* popover = sheet.popoverPresentationController;
  if (popover) {
    popover.sourceView = self.view;
    popover.sourceRect =
        CGRectMake(CGRectGetMidX(self.view.bounds), CGRectGetMidY(self.view.bounds), 1.0, 1.0);
    popover.permittedArrowDirections = 0;
  }

  UIViewController* presenter = self;
  while (presenter.presentedViewController) {
    presenter = presenter.presentedViewController;
  }
  [presenter presentViewController:sheet animated:YES completion:nil];
}

- (void)presentAchievementsForUserIndex:(uint32_t)user_index
                                titleID:(uint32_t)title_id
                             completion:(void (^)(BOOL success))completion {
  if (!self.appContext) {
    if (completion) {
      completion(NO);
    }
    return;
  }

  XeniaAchievementsViewController* achievements_vc =
      [[XeniaAchievementsViewController alloc] initWithAppContext:self.appContext
                                                        userIndex:user_index
                                                          titleID:title_id];
  __block XeniaViewController* unsafe_self = self;
  achievements_vc.dismissalHandler = ^{
    [unsafe_self updateTouchControlsOverlayVisibilityAnimated:YES];
  };
  [self presentPauseSheetWithRootController:achievements_vc
                             preferredSize:CGSizeMake(560.0, 620.0)
               preventInteractiveDismissal:NO
                                completion:^{
                                  if (completion) {
                                    completion(YES);
                                  }
                                }];
  [achievements_vc release];
}

- (void)presentAchievementNotification:(const xe::ui::AchievementNotificationPayload&)payload {
  [self.achievementNotificationPresenter presentPayload:payload inView:self.view];
}

- (void)presentSystemKeyboardPromptWithTitle:(NSString*)title
                                 description:(NSString*)description
                                 defaultText:(NSString*)default_text
                                  completion:(void (^)(BOOL cancelled, NSString* text))completion {
  UIAlertController* alert =
      [UIAlertController alertControllerWithTitle:title.length ? title : @"Input Required"
                                          message:description
                                   preferredStyle:UIAlertControllerStyleAlert];
  [alert addTextFieldWithConfigurationHandler:^(UITextField* text_field) {
    text_field.text = default_text ?: @"";
    text_field.autocapitalizationType = UITextAutocapitalizationTypeNone;
    text_field.autocorrectionType = UITextAutocorrectionTypeNo;
    text_field.clearButtonMode = UITextFieldViewModeWhileEditing;
  }];

  [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                            style:UIAlertActionStyleCancel
                                          handler:^(__unused UIAlertAction* action) {
                                            if (completion) {
                                              completion(YES, @"");
                                            }
                                          }]];
  [alert addAction:[UIAlertAction actionWithTitle:@"OK"
                                            style:UIAlertActionStyleDefault
                                          handler:^(__unused UIAlertAction* action) {
                                            UITextField* text_field = alert.textFields.firstObject;
                                            NSString* text = text_field.text ?: @"";
                                            if (completion) {
                                              completion(NO, text);
                                            }
                                          }]];

  UIViewController* presenter = self;
  while (presenter.presentedViewController) {
    presenter = presenter.presentedViewController;
  }
  [presenter presentViewController:alert animated:YES completion:nil];
}

- (void)updateJITStatusIndicator {
  [self.launcherOverlayView setJITAcquired:self.jitAcquired];
  if (self.jitAcquired) {
    [self.launcherOverlayView setJITStatusText:@"JIT Enabled"];
  } else if (self.jitPollTimedOut) {
    [self.launcherOverlayView
        setJITStatusText:@"JIT unavailable. Open StikDebug, then restart polling."];
  } else {
    [self.launcherOverlayView setJITStatusText:xe_jit_waiting_status_message()];
  }
}

- (void)updateJITAvailabilityUI {
  [self.launcherOverlayView setJITAcquired:self.jitAcquired];
}

- (std::filesystem::path)importedGamesDirectory {
  return IOSImportedGamesDirectory();
}

- (std::filesystem::path)importGameIntoLibrary:(NSURL*)source_url error:(NSError**)error {
  return ImportGameIntoIOSLibrary(source_url, error);
}

- (void)refreshImportedGames {
  ++library_refresh_generation_;
  // Load cached title names populated by previous game launches.
  NSString* caches_dir =
      NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject;
  NSString* names_path = [caches_dir stringByAppendingPathComponent:@"title-names.plist"];
  NSDictionary* title_name_cache = [[NSDictionary dictionaryWithContentsOfFile:names_path] retain];

  discovered_games_ = ScanIOSGameLibrary(title_name_cache);
  [title_name_cache release];

  [self finishImportedGamesRefresh];
}

- (void)refreshImportedGamesAsync {
  const uint64_t refresh_generation = ++library_refresh_generation_;
  NSString* caches_dir =
      NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject;
  NSString* names_path = [caches_dir stringByAppendingPathComponent:@"title-names.plist"];
  NSDictionary* title_name_cache = [[NSDictionary dictionaryWithContentsOfFile:names_path] retain];

  __unsafe_unretained XeniaViewController* unsafe_self = self;
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    auto* scanned_games = new std::vector<IOSDiscoveredGame>(ScanIOSGameLibrary(title_name_cache));
    [title_name_cache release];
    dispatch_async(dispatch_get_main_queue(), ^{
      std::unique_ptr<std::vector<IOSDiscoveredGame>> scanned_games_owner(scanned_games);
      if (refresh_generation != unsafe_self->library_refresh_generation_) {
        return;
      }
      unsafe_self->discovered_games_ = std::move(*scanned_games_owner);
      [unsafe_self finishImportedGamesRefresh];
    });
  });
}

- (void)finishImportedGamesRefresh {
  [self applyCompatDataToDiscoveredGames];
  SortDiscoveredGames(&discovered_games_);

  NSInteger focused_game_index = self.controllerNavigationCoordinator.focusedGameIndex;
  if (discovered_games_.empty()) {
    focused_game_index = -1;
  } else if (focused_game_index < 0 ||
             focused_game_index >= static_cast<NSInteger>(discovered_games_.size())) {
    focused_game_index = 0;
  }
  [self.controllerNavigationCoordinator setFocusedGameIndex:focused_game_index scroll:NO];
  [self refreshLauncherGameSnapshots];
  [self.controllerNavigationCoordinator refreshLauncherFocus];
}

- (void)applyCompatDataToDiscoveredGames {
  ApplyIOSCompatibilityDataToDiscoveredGames(compat_data_, &discovered_games_);
}

- (void)presentJITRequiredAlert {
  UIAlertController* alert =
      [UIAlertController alertControllerWithTitle:@"JIT Not Detected"
                                          message:xe_jit_not_detected_guidance_message()
                                   preferredStyle:UIAlertControllerStyleAlert];
  [alert addAction:[UIAlertAction actionWithTitle:@"Open Settings"
                                            style:UIAlertActionStyleDefault
                                          handler:^(__unused UIAlertAction* action) {
                                            [self openSettingsTapped:nil];
                                          }]];
  [alert addAction:[UIAlertAction actionWithTitle:@"OK"
                                            style:UIAlertActionStyleCancel
                                          handler:nil]];
  [self presentViewController:alert animated:YES completion:nil];
}

- (NSString*)displayNameForGamePath:(const std::filesystem::path&)game_path {
  for (const IOSDiscoveredGame& game : discovered_games_) {
    if (game.path == game_path) {
      return ToNSString(game.title);
    }
    for (const auto& disc : game.discs) {
      if (disc.path == game_path) {
        return ToNSString(game.title);
      }
    }
  }
  return nil;
}

- (BOOL)findDiscoveredGameWithTitleID:(uint32_t)title_id
                               system:(const xe::ui::IOSGameSystem*)system_filter
                                 path:(std::filesystem::path*)path_out
                          displayName:(NSString**)display_name_out {
  if (!title_id || !path_out) {
    return NO;
  }

  auto find_match = [&]() -> const IOSDiscoveredGame* {
    for (const IOSDiscoveredGame& game : discovered_games_) {
      if (game.title_id == title_id && (!system_filter || game.system == *system_filter)) {
        return &game;
      }
    }
    return nullptr;
  };

  const IOSDiscoveredGame* match = find_match();
  if (!match) {
    [self refreshImportedGames];
    match = find_match();
  }
  if (!match) {
    return NO;
  }

  *path_out = match->path;
  if (display_name_out) {
    *display_name_out = ToNSString(match->title);
  }
  return YES;
}

- (BOOL)requestAutomaticStikDebugJITHandoffForPendingLaunchPath:
    (const std::filesystem::path*)launch_path {
  if (!GetUserDefaultBool(kXeniaAutoOpenStikDebugOnLaunchPreferenceKey, false)) {
    XELOGI("iOS: Automatic StikDebug handoff skipped (disabled)");
    return NO;
  }
  if (self.gameRunning || self.gameStopInProgress) {
    XELOGI("iOS: Automatic StikDebug handoff skipped (game already running)");
    return NO;
  }
  // Don't bounce through StikDebug while a touch layout install confirm is
  // pending. Layouts don't need JIT, and the app-switch round-trip kills the
  // modal before the user can act on it. The handoff gets re-evaluated once
  // the install confirm is dismissed.
  if ([self.touchLayoutCoordinator hasPendingInstall]) {
    XELOGI("iOS: Automatic StikDebug handoff deferred (touch layout install pending)");
    return NO;
  }
  if (self.jitAcquired || xe_check_jit_available()) {
    if (!self.jitAcquired) {
      [self onJITAcquired];
    }
    XELOGI("iOS: Automatic StikDebug handoff skipped (JIT already available)");
    return NO;
  }

  const double now = GetUnixTimeSeconds();
  const double last_attempt =
      GetUserDefaultDouble(kXeniaLastAutoStikDebugAttemptTimestampPreferenceKey, 0.0);
  if (last_attempt > 0.0 && (now - last_attempt) < kXeniaAutoStikDebugCooldownSeconds) {
    XELOGI("iOS: Skipping automatic StikDebug handoff (cooldown active)");
    return NO;
  }

  NSString* bundle_identifier = NSBundle.mainBundle.bundleIdentifier;
  NSURL* stikdebug_url = xe_stikdebug_enable_jit_url_for_bundle_identifier(bundle_identifier);
  if (!stikdebug_url) {
    XELOGW("iOS: Unable to build StikDebug JIT handoff URL");
    return NO;
  }

  UIApplication* application = [UIApplication sharedApplication];
  if (![application canOpenURL:stikdebug_url]) {
    XELOGW("iOS: StikDebug URL scheme unavailable");
    [self showStatusToast:@"StikDebug is not installed or unavailable."
                    style:XeniaIOSStatusToastStyleError];
    if (launch_path && !launch_path->empty()) {
      ClearPendingExternalLaunchPathPreference();
    }
    return NO;
  }

  if (launch_path && !launch_path->empty()) {
    StorePendingExternalLaunchPathPreference(*launch_path);
  }

  const BOOL has_pending_launch = launch_path && !launch_path->empty();
  SetUserDefaultDouble(kXeniaLastAutoStikDebugAttemptTimestampPreferenceKey, now);
  [self showStatusToast:has_pending_launch ? @"Opening StikDebug to enable JIT..."
                                           : @"Opening StikDebug for JIT..."
                  style:XeniaIOSStatusToastStyleInfo];
  XELOGI("iOS: Opening StikDebug handoff URL {}", stikdebug_url.absoluteString.UTF8String);

  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.15 * NSEC_PER_SEC)),
                 dispatch_get_main_queue(), ^{
                   [application openURL:stikdebug_url
                       options:@{}
                       completionHandler:^(BOOL success) {
                         if (!success) {
                           XELOGW("iOS: Failed to open StikDebug handoff URL");
                           [self showStatusToast:@"Failed to open StikDebug."
                                           style:XeniaIOSStatusToastStyleError];
                           if (has_pending_launch) {
                             ClearPendingExternalLaunchPathPreference();
                           }
                         }
                       }];
                 });
  return YES;
}

- (void)evaluateAutomaticStikDebugJITHandoffIfNeeded {
  [self requestAutomaticStikDebugJITHandoffForPendingLaunchPath:nullptr];
}

- (void)copyLaunchURLForGameAtIndex:(size_t)game_index {
  if (game_index >= discovered_games_.size()) {
    return;
  }
  const IOSDiscoveredGame& game = discovered_games_[game_index];
  if (!game.title_id) {
    [self showStatusToast:@"Launch URL unavailable for this game."
                    style:XeniaIOSStatusToastStyleError];
    return;
  }

  NSString* launch_url = xe_launch_url_for_title_id(game.title_id, game.system);
  if (!launch_url || launch_url.length == 0) {
    [self showStatusToast:@"Failed to build launch URL." style:XeniaIOSStatusToastStyleError];
    return;
  }

  [UIPasteboard generalPasteboard].string = launch_url;
  NSString* game_title = ToNSString(game.title);
  NSString* message = game_title.length > 0
                          ? [NSString stringWithFormat:@"Copied launch URL for %@.", game_title]
                          : @"Copied launch URL.";
  [self showStatusToast:message style:XeniaIOSStatusToastStyleSuccess];
  XELOGI("iOS: Copied title-ID launch URL {}", [launch_url UTF8String]);
}

- (BOOL)respondToExternalGameInfoRequestURL:(NSURL*)url {
  NSString* callback_scheme = nil;
  if (!IsExternalGameInfoRequestURL(url, &callback_scheme)) {
    return NO;
  }

  if (!callback_scheme || callback_scheme.length == 0) {
    XELOGW("iOS: gameInfo request is missing callback scheme");
    [self showStatusToast:@"gameInfo request is missing a callback scheme."
                    style:XeniaIOSStatusToastStyleError];
    return YES;
  }

  NSString* request_scheme = NormalizeURLToken(url.scheme);
  if (request_scheme && [callback_scheme caseInsensitiveCompare:request_scheme] == NSOrderedSame) {
    XELOGW("iOS: gameInfo callback scheme {} would loop back into XeniOS",
           [callback_scheme UTF8String]);
    [self showStatusToast:@"gameInfo callback scheme cannot point back to XeniOS."
                    style:XeniaIOSStatusToastStyleError];
    return YES;
  }

  [self refreshImportedGames];

  NSMutableArray* exported_games = [NSMutableArray array];
  size_t skipped_without_title_id = 0;
  for (const IOSDiscoveredGame& game : discovered_games_) {
    if (!game.title_id) {
      ++skipped_without_title_id;
      continue;
    }

    NSString* title_id = ToNSString(FormatTitleID(game.title_id));
    NSString* title_name =
        game.title.empty() ? ToNSString(game.path.stem().string()) : ToNSString(game.title);
    NSString* icon_base64 = @"";
    if (!game.icon_data.empty()) {
      NSData* icon_data = [NSData dataWithBytes:game.icon_data.data() length:game.icon_data.size()];
      if (icon_data.length > 0) {
        icon_base64 = [icon_data base64EncodedStringWithOptions:0] ?: @"";
      }
    }

    NSDictionary* entry = @{
      @"titleName" : title_name ?: @"",
      @"version" : @"",
      @"iconData" : icon_base64 ?: @"",
      @"titleId" : title_id ?: @"",
      @"id" : title_id ?: @"",
      @"developer" : @"",
    };
    [exported_games addObject:entry];
  }

  NSError* json_error = nil;
  NSData* json_data = [NSJSONSerialization dataWithJSONObject:exported_games
                                                      options:0
                                                        error:&json_error];
  if (!json_data || json_error) {
    XELOGE("iOS: Failed serializing gameInfo payload: {}",
           json_error.localizedDescription.UTF8String);
    [self showStatusToast:@"Failed to build gameInfo payload." style:XeniaIOSStatusToastStyleError];
    return YES;
  }

  NSString* games_payload = [json_data base64EncodedStringWithOptions:0];
  if (!games_payload || games_payload.length == 0) {
    XELOGW("iOS: gameInfo payload encoding produced empty data");
    [self showStatusToast:@"Failed to encode gameInfo payload."
                    style:XeniaIOSStatusToastStyleError];
    return YES;
  }

  NSURLComponents* callback_components = [[[NSURLComponents alloc] init] autorelease];
  callback_components.scheme = callback_scheme;
  callback_components.host = xe_game_info_callback_provider(url);
  callback_components.queryItems = @[ [NSURLQueryItem queryItemWithName:@"games"
                                                                  value:games_payload] ];
  NSURL* callback_url = callback_components.URL;
  if (!callback_url) {
    XELOGE("iOS: Failed building gameInfo callback URL for scheme {}",
           [callback_scheme UTF8String]);
    [self showStatusToast:@"Failed to build gameInfo callback URL."
                    style:XeniaIOSStatusToastStyleError];
    return YES;
  }

  const NSUInteger exported_count = exported_games.count;
  NSString* callback_app = callback_scheme;
  XELOGI(
      "iOS: Returning {} games via {} (skipped {} without title IDs, payload {} bytes, URL chars "
      "{})",
      static_cast<uint32_t>(exported_count), callback_url.absoluteString.UTF8String,
      static_cast<uint32_t>(skipped_without_title_id), static_cast<uint32_t>(json_data.length),
      static_cast<uint32_t>(callback_url.absoluteString.length));

  [[UIApplication sharedApplication]
                openURL:callback_url
                options:@{}
      completionHandler:^(BOOL success) {
        if (success) {
          [self showStatusToast:[NSString stringWithFormat:@"Sent %lu games to %@.",
                                                           (unsigned long)exported_count,
                                                           callback_app]
                          style:XeniaIOSStatusToastStyleSuccess];
          return;
        }
        XELOGW("iOS: Failed opening gameInfo callback URL {}",
               callback_url.absoluteString.UTF8String);
        [self showStatusToast:[NSString
                                  stringWithFormat:@"Failed to return library to %@.", callback_app]
                        style:XeniaIOSStatusToastStyleError];
      }];
  return YES;
}

- (BOOL)handleExternalLaunchURL:(NSURL*)url {
  if ([self respondToExternalGameInfoRequestURL:url]) {
    return YES;
  }

  // Touch-layout file URLs and xenios://touchlayout?... drops share the
  // launch URL surface, so handle them before the game-launch branches.
  if ([self handleExternalTouchLayoutFileURL:url]) {
    return YES;
  }
  if ([self handleExternalTouchLayoutSchemeURL:url]) {
    return YES;
  }

  std::filesystem::path launch_path;
  NSString* display_name = nil;
  uint32_t title_id = 0;
  xe::ui::IOSGameSystem title_system = xe::ui::IOSGameSystem::kXbox360;
  bool title_system_present = false;
  if (ExtractLaunchPathFromExternalURL(url, &launch_path) && !launch_path.empty()) {
    display_name = [self displayNameForGamePath:launch_path];
    if (!display_name || display_name.length == 0) {
      display_name = ToNSString(launch_path.filename().string());
    }
  } else if (ExtractLaunchTitleIDFromExternalURL(url, &title_id, &title_system,
                                                 &title_system_present) &&
             title_id) {
    if (![self findDiscoveredGameWithTitleID:title_id
                                      system:title_system_present ? &title_system : nullptr
                                        path:&launch_path
                                 displayName:&display_name]) {
      XELOGW("iOS: External launch title ID {:08X} was not found in Library", title_id);
      [self showStatusToast:[NSString stringWithFormat:@"Title ID %08X was not found in Library.",
                                                       title_id]
                      style:XeniaIOSStatusToastStyleError];
      return NO;
    }
  }

  if (launch_path.empty()) {
    NSString* absolute_url = [url absoluteString];
    XELOGW("iOS: External launch URL missing valid game target: {}",
           absolute_url ? [absolute_url UTF8String] : "");
    [self showStatusToast:@"Launch URL missing a valid game target."
                    style:XeniaIOSStatusToastStyleError];
    return NO;
  }

  if (!display_name || display_name.length == 0) {
    display_name = ToNSString(launch_path.filename().string());
  }

  if (title_id) {
    XELOGI("iOS: External game launch requested by title ID {:08X}: {}", title_id,
           launch_path.string());
  } else {
    XELOGI("iOS: External game launch requested: {}", launch_path.string());
  }
  if (!self.jitAcquired) {
    pending_external_launch_path_ = launch_path;
    if (![self requestAutomaticStikDebugJITHandoffForPendingLaunchPath:&launch_path]) {
      [self
          showStatusToast:[NSString stringWithFormat:@"Waiting for JIT to launch: %@", display_name]
                    style:XeniaIOSStatusToastStyleInfo];
    }
    return YES;
  }

  [self launchGameAtPath:launch_path displayName:display_name];
  return YES;
}

- (void)launchGameAtPath:(const std::filesystem::path&)game_path
             displayName:(NSString*)display_name {
  (void)display_name;
  NSString* path_ns = ToNSString(game_path.string());
  std::error_code path_exists_ec;
  if (!std::filesystem::exists(game_path, path_exists_ec)) {
    [self showStatusToast:@"Game file is no longer available."
                    style:XeniaIOSStatusToastStyleError];
    [self refreshImportedGamesAsync];
    return;
  }

  if (IsLikelyGodContainerFile(game_path)) {
    auto metadata = xe::vfs::ExtractStfsMetadata(game_path);
    if (metadata.has_value() && metadata->data_file_count > 0 &&
        !HasContentSidecarDataDirectory(game_path)) {
      [self showStatusToast:@"Selected game is missing its .data folder."
                      style:XeniaIOSStatusToastStyleError];
      XEPresentOKAlert(self, @"Missing Game Data",
                       @"This GOD package needs its matching .data folder in the same directory. "
                       @"Transfer both the GOD file and its .data folder, then try again.");
      return;
    }
  }

  if (!self.jitAcquired) {
    [self presentJITRequiredAlert];
    return;
  }

  if (self.gameStopInProgress || self.gameRunning) {
    if (self.appContext) {
      self.gameRunning = YES;
      self.gameStopInProgress = NO;
      active_game_title_id_ = [self titleIDForGamePath:game_path];
      [self applyTouchLayoutModelForTitleID:active_game_title_id_];
      xe_request_landscape_orientation(self);
      [UIView animateWithDuration:0.3
          animations:^{
            self.launcherOverlayView.alpha = 0.0;
          }
          completion:^(__unused BOOL finished) {
            self.launcherOverlayView.hidden = YES;
            [self updateTouchControlsOverlayVisibilityAnimated:YES];
          }];
      self.appContext->LaunchGame(std::string([path_ns UTF8String]));
    } else {
      [self showStatusToast:@"Unable to queue launch (app context unavailable)."
                      style:XeniaIOSStatusToastStyleError];
    }
    return;
  }

  self.gameRunning = YES;
  active_game_title_id_ = [self titleIDForGamePath:game_path];
  [self applyTouchLayoutModelForTitleID:active_game_title_id_];

  xe_request_landscape_orientation(self);
  [UIView animateWithDuration:0.3
      animations:^{
        self.launcherOverlayView.alpha = 0.0;
      }
      completion:^(__unused BOOL finished) {
        self.launcherOverlayView.hidden = YES;
        [self updateTouchControlsOverlayVisibilityAnimated:YES];
      }];

  if (self.appContext) {
    self.appContext->LaunchGame(std::string([path_ns UTF8String]));
  } else {
    [self showStatusToast:@"Unable to launch game (app context unavailable)."
                    style:XeniaIOSStatusToastStyleError];
    self.launcherOverlayView.hidden = NO;
    self.launcherOverlayView.alpha = 1.0;
    [self updateTouchControlsOverlayVisibilityAnimated:YES];
  }
}

- (BOOL)installTitleUpdateAtPath:(NSString*)path
                          status:(NSString**)status_out
                  notTitleUpdate:(BOOL*)not_title_update_out {
  if (status_out) {
    *status_out = nil;
  }
  if (not_title_update_out) {
    *not_title_update_out = NO;
  }
  if (!self.appContext) {
    if (status_out) {
      *status_out = @"App context unavailable.";
    }
    return NO;
  }

  std::string status;
  bool not_title_update = false;
  BOOL success = self.appContext->InstallTitleUpdate(std::string([path UTF8String]), &status,
                                                     &not_title_update);
  if (status_out && !status.empty()) {
    *status_out = ToNSString(status);
  }
  if (not_title_update_out) {
    *not_title_update_out = not_title_update;
  }
  return success;
}

- (void)presentGameActionsSheetForIndex:(size_t)game_index {
  if (game_index >= discovered_games_.size()) {
    return;
  }

  const IOSDiscoveredGame& game = discovered_games_[game_index];
  NSString* game_title =
      game.title.empty() ? ToNSString(game.path.stem().string()) : ToNSString(game.title);
  XeniaIOSGameActionsViewController* actions_controller =
      [[XeniaIOSGameActionsViewController alloc]
                    initWithGameTitle:game_title
                              titleID:game.title_id
                supportsCompatibility:game.title_id != 0 &&
                                      xe_game_system_supports_compatibility(game.system)
                 supportsManageContent:game.title_id != 0 &&
                                       xe_game_system_supports_manage_content(game.system)
                 supportsDiscSelection:game.discs.size() > 1
                      supportsPatches:self.appContext && game.title_id != 0];

  __unsafe_unretained XeniaViewController* unsafe_self = self;
  actions_controller.actionHandler = ^(XeniaIOSGameAction action) {
    [unsafe_self dismissViewControllerAnimated:YES
                                    completion:^{
                                      [unsafe_self performGameAction:action forIndex:game_index];
                                    }];
  };

  XeniaLandscapeNavigationController* navigation_controller =
      [[XeniaLandscapeNavigationController alloc] initWithRootViewController:actions_controller];
  XEConfigureDestinationPresentation(navigation_controller, self.view, CGSizeMake(520.0, 620.0),
                                     NO);
  [self presentViewController:navigation_controller animated:YES completion:nil];
  [navigation_controller release];
  [actions_controller release];
}

- (void)performGameAction:(XeniaIOSGameAction)action forIndex:(size_t)game_index {
  if (game_index >= discovered_games_.size()) {
    return;
  }

  switch (action) {
    case XeniaIOSGameActionPlay: {
      const IOSDiscoveredGame& game = discovered_games_[game_index];
      [self launchGameAtPath:game.path displayName:ToNSString(game.title)];
    } break;
    case XeniaIOSGameActionGameSettings:
      [self presentGameSettingsSheetForIndex:game_index];
      break;
    case XeniaIOSGameActionTouchLayout:
      [self presentGameTouchLayoutSheetForIndex:game_index];
      break;
    case XeniaIOSGameActionCompatibility:
      [self presentCompatibilitySheetForIndex:game_index];
      break;
    case XeniaIOSGameActionManageContent:
      [self presentManageContentSheetForIndex:game_index];
      break;
    case XeniaIOSGameActionLaunchDisc:
      [self presentDiscSelectionSheetForIndex:game_index];
      break;
    case XeniaIOSGameActionPatches:
      [self presentPatchesSheetForIndex:game_index];
      break;
    case XeniaIOSGameActionCopyLaunchURL:
      [self copyLaunchURLForGameAtIndex:game_index];
      break;
  }
}

- (void)presentTouchLayoutGamePicker {
  NSMutableArray<XeniaIOSGamePickerItem*>* items =
      [NSMutableArray arrayWithCapacity:discovered_games_.size()];
  for (size_t i = 0; i < discovered_games_.size(); ++i) {
    const IOSDiscoveredGame& game = discovered_games_[i];
    if (!game.title_id) {
      continue;
    }

    NSString* title =
        game.title.empty() ? ToNSString(game.path.stem().string()) : ToNSString(game.title);
    NSMutableArray<NSString*>* details = [NSMutableArray array];
    [details addObject:[NSString stringWithFormat:@"Title ID %@",
                                                  ToNSString(FormatTitleID(game.title_id))]];
    if (!game.content_type_name.empty()) {
      [details addObject:ToNSString(game.content_type_name)];
    } else if (game.discs.size() > 1) {
      [details addObject:[NSString stringWithFormat:@"%zu discs", game.discs.size()]];
    }

    [items addObject:[XeniaIOSGamePickerItem
                         itemWithTitle:title
                              subtitle:[details componentsJoinedByString:@" · "]
                               titleID:game.title_id
                             gameIndex:static_cast<NSUInteger>(i)]];
  }

  if (items.count == 0) {
    XEPresentOKAlert(self, @"No Games Available",
                     @"Import a game with a title ID before editing a per-game touch layout.");
    return;
  }

  XeniaIOSGamePickerViewController* picker =
      [[XeniaIOSGamePickerViewController alloc]
          initWithTitle:@"Game Touch Layout"
                 prompt:@"Choose a title to edit before launch."
                  items:items];
  __unsafe_unretained XeniaViewController* unsafe_self = self;
  picker.selectionHandler = ^(NSUInteger gameIndex) {
    [unsafe_self dismissViewControllerAnimated:YES
                                    completion:^{
                                      [unsafe_self presentGameTouchLayoutSheetForIndex:gameIndex];
                                    }];
  };

  XeniaLandscapeNavigationController* navigation_controller =
      [[XeniaLandscapeNavigationController alloc] initWithRootViewController:picker];
  XEConfigureDestinationPresentation(navigation_controller, self.view, CGSizeMake(560.0, 700.0),
                                     NO);
  UIViewController* presenter = [self topPresentedControllerForModalPresentation] ?: self;
  [presenter presentViewController:navigation_controller animated:YES completion:nil];
  [navigation_controller release];
  [picker release];
}

- (void)presentGameSettingsSheetForIndex:(size_t)game_index {
  if (game_index >= discovered_games_.size()) {
    return;
  }

  const IOSDiscoveredGame& game = discovered_games_[game_index];
  if (!game.title_id) {
    XEPresentOKAlert(self, @"Unavailable",
                     @"This item does not expose a title ID, so game settings cannot be saved.");
    return;
  }

  NSString* game_title =
      game.title.empty() ? ToNSString(game.path.stem().string()) : ToNSString(game.title);
  XeniaConfigViewController* config_controller = [[XeniaConfigViewController alloc]
      initWithCatalogKind:IOSConfigCatalogKind::kPerGame
                    style:UITableViewStyleInsetGrouped
              gameTitleID:game.title_id
                gameTitle:game_title];
  XeniaLandscapeNavigationController* navigation_controller =
      [[XeniaLandscapeNavigationController alloc] initWithRootViewController:config_controller];
  XEConfigureDestinationPresentation(navigation_controller, self.view, CGSizeMake(640.0, 760.0),
                                     NO);
  [self presentViewController:navigation_controller animated:YES completion:nil];
  [navigation_controller release];
  [config_controller release];
}

- (void)presentGameTouchLayoutSheetForIndex:(size_t)game_index {
  if (game_index >= discovered_games_.size()) {
    return;
  }

  const IOSDiscoveredGame& game = discovered_games_[game_index];
  if (!game.title_id) {
    XEPresentOKAlert(self, @"Unavailable",
                     @"This item does not expose a title ID, so a title-specific touch layout "
                     @"cannot be saved.");
    return;
  }

  NSString* game_title =
      game.title.empty() ? ToNSString(game.path.stem().string()) : ToNSString(game.title);
  XeniaIOSTouchLayoutEditorViewController* editor_controller =
      [[XeniaIOSTouchLayoutEditorViewController alloc] initWithTitleID:game.title_id
                                                                 title:game_title];
  XeniaLandscapeNavigationController* navigation_controller =
      [[XeniaLandscapeNavigationController alloc] initWithRootViewController:editor_controller];
  navigation_controller.modalPresentationStyle = UIModalPresentationFullScreen;
  navigation_controller.navigationBarHidden = YES;
  [self presentViewController:navigation_controller animated:YES completion:nil];
  [navigation_controller release];
  [editor_controller release];
}

- (void)presentCompatibilitySheetForIndex:(size_t)game_index {
  if (game_index >= discovered_games_.size()) {
    return;
  }

  const IOSDiscoveredGame& game = discovered_games_[game_index];
  if (!game.title_id) {
    XEPresentOKAlert(self, @"Unavailable",
                     @"This item does not expose a title ID, so compatibility details "
                     @"cannot be loaded.");
    return;
  }

  NSDictionary* compat_data = [compat_data_ objectForKey:XEFormatTitleIDHexUpper(game.title_id)];
  NSString* game_title =
      game.title.empty() ? ToNSString(game.path.stem().string()) : ToNSString(game.title);
  UIImage* hero_artwork = xe_cached_game_art(game.title_id);
  if (!hero_artwork && !game.icon_data.empty()) {
    NSData* data = [NSData dataWithBytes:game.icon_data.data() length:game.icon_data.size()];
    hero_artwork = [UIImage imageWithData:data];
  }
  if (!hero_artwork && self.launcherOverlayView.gamesCollectionView) {
    NSIndexPath* index_path = [NSIndexPath indexPathForItem:(NSInteger)game_index inSection:0];
    XeniaGameTileCell* tile = (XeniaGameTileCell*)[self.launcherOverlayView.gamesCollectionView
        cellForItemAtIndexPath:index_path];
    if ([tile isKindOfClass:[XeniaGameTileCell class]]) {
      hero_artwork = tile.iconView.image;
    }
  }
  XeniaGameCompatibilityViewController* compatibility_controller =
      [[XeniaGameCompatibilityViewController alloc] initWithTitleID:game.title_id
                                                              title:game_title
                                                         compatData:compat_data];
  if (hero_artwork) {
    [compatibility_controller setHeroArtwork:hero_artwork];
  }
  XeniaLandscapeNavigationController* navigation_controller =
      [[XeniaLandscapeNavigationController alloc]
          initWithRootViewController:compatibility_controller];
  navigation_controller.view.backgroundColor = [XeniaTheme bgPrimary];
  XEConfigureDestinationPresentation(navigation_controller, self.view, CGSizeMake(620.0, 720.0),
                                     NO);
  [self presentViewController:navigation_controller animated:YES completion:nil];
  [navigation_controller release];
  [compatibility_controller release];
}

- (void)presentManageContentSheetForIndex:(size_t)game_index {
  if (game_index >= discovered_games_.size()) {
    return;
  }

  const IOSDiscoveredGame& game = discovered_games_[game_index];
  if (!game.title_id) {
    XEPresentOKAlert(
        self, @"Unavailable",
        @"This item does not expose a title ID, so installed content cannot be managed.");
    return;
  }

  XeniaGameContentViewController* content_controller = [[XeniaGameContentViewController alloc]
      initWithTitleID:game.title_id
                title:(game.title.empty() ? ToNSString(game.path.stem().string())
                                          : ToNSString(game.title))host:self];
  XeniaLandscapeNavigationController* navigation_controller =
      [[XeniaLandscapeNavigationController alloc] initWithRootViewController:content_controller];
  XEConfigureDestinationPresentation(navigation_controller, self.view, CGSizeMake(600.0, 680.0),
                                     NO);
  [self presentViewController:navigation_controller animated:YES completion:nil];
  [navigation_controller release];
  [content_controller release];
}

- (void)presentDiscSelectionSheetForIndex:(size_t)game_index {
  if (game_index >= discovered_games_.size()) {
    return;
  }

  const IOSDiscoveredGame& game = discovered_games_[game_index];
  if (game.discs.size() <= 1) {
    [self launchGameAtPath:game.path displayName:ToNSString(game.title)];
    return;
  }

  NSString* game_title =
      game.title.empty() ? ToNSString(game.path.stem().string()) : ToNSString(game.title);
  __unsafe_unretained XeniaViewController* unsafe_self = self;
  XeniaGameDiscViewController* disc_controller = [[XeniaGameDiscViewController alloc]
         initWithTitle:game_title
                 discs:game.discs
      selectionHandler:^(NSString* path, NSString* label) {
        if (!path.length) {
          return;
        }
        NSString* display_name =
            label.length ? [NSString stringWithFormat:@"%@ (%@)", game_title, label] : game_title;
        [unsafe_self launchGameAtPath:std::filesystem::path([path UTF8String])
                          displayName:display_name];
      }];
  XeniaLandscapeNavigationController* navigation_controller =
      [[XeniaLandscapeNavigationController alloc] initWithRootViewController:disc_controller];
  XEConfigureDestinationPresentation(navigation_controller, self.view, CGSizeMake(520.0, 560.0),
                                     NO);
  [self presentViewController:navigation_controller animated:YES completion:nil];
  [navigation_controller release];
  [disc_controller release];
}

- (void)presentPatchesSheetForIndex:(size_t)game_index {
  if (game_index >= discovered_games_.size()) {
    return;
  }

  const IOSDiscoveredGame& game = discovered_games_[game_index];
  if (!game.title_id) {
    XEPresentOKAlert(self, @"Unavailable",
                     @"This item does not expose a title ID, so patches cannot be loaded.");
    return;
  }
  if (!self.appContext) {
    XEPresentOKAlert(self, @"Unavailable", @"Patch management is unavailable.");
    return;
  }

  NSString* game_title =
      game.title.empty() ? ToNSString(game.path.stem().string()) : ToNSString(game.title);
  XeniaGamePatchesViewController* patches_controller =
      [[XeniaGamePatchesViewController alloc] initWithTitleID:game.title_id
                                                        title:game_title
                                                   appContext:self.appContext];
  XeniaLandscapeNavigationController* navigation_controller =
      [[XeniaLandscapeNavigationController alloc] initWithRootViewController:patches_controller];
  XEConfigureDestinationPresentation(navigation_controller, self.view, CGSizeMake(600.0, 680.0),
                                     NO);
  [self presentViewController:navigation_controller animated:YES completion:nil];
  [navigation_controller release];
  [patches_controller release];
}

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
  CGSize collection_size = self.launcherOverlayView.gamesCollectionView.bounds.size;
  if (!CGSizeEqualToSize(collection_size, last_collection_layout_size_)) {
    last_collection_layout_size_ = collection_size;
    [self.launcherOverlayView.gamesCollectionView.collectionViewLayout invalidateLayout];
  }
  // Keep scrollable content above the home indicator. The collection view's
  // visual frame extends to the screen bottom (so the launcher background
  // doesn't leave a white bar there), but cells should stop scrolling at the
  // safe area to keep the bottom row clear of the home gesture zone.
  CGFloat home_inset = self.view.safeAreaInsets.bottom;
  UIEdgeInsets current = self.launcherOverlayView.gamesCollectionView.contentInset;
  if (current.bottom != home_inset) {
    current.bottom = home_inset;
    self.launcherOverlayView.gamesCollectionView.contentInset = current;
    UIEdgeInsets indicator = self.launcherOverlayView.gamesCollectionView.scrollIndicatorInsets;
    indicator.bottom = home_inset;
    self.launcherOverlayView.gamesCollectionView.scrollIndicatorInsets = indicator;
  }
  [self applyMetalViewLayout];
  // Notify the app context that the layout changed, so the window and
  // presenter can update for rotation, split-view, or safe-area changes.
  if (self.appContext) {
    self.appContext->NotifyLayoutChanged();
  }
}

- (void)refreshLauncherChromeForCurrentTraits {
  [self.launcherOverlayView refreshChromeForCurrentTraits];
  [self.controllerNavigationCoordinator refreshLauncherFocus];
}

- (void)traitCollectionDidChange:(UITraitCollection*)previousTraitCollection {
  [super traitCollectionDidChange:previousTraitCollection];
  if ([self.traitCollection
          hasDifferentColorAppearanceComparedToTraitCollection:previousTraitCollection]) {
    [self refreshLauncherChromeForCurrentTraits];
  }
}

// External touch-layout install URL safety limits. 64 KB is well over the
// largest official preset and still small enough to reject hostile TOML drops
// before they can spend meaningful CPU time in the parser.
static constexpr NSUInteger kXeniaIOSTouchLayoutMaxBytes = 64 * 1024;
static constexpr NSUInteger kXeniaIOSTouchLayoutURLMaxLength = 2048;

#pragma mark - Window scaling

- (XeniaIOSWindowScalingMode)currentWindowScalingMode {
  return XeniaIOSCurrentWindowScalingMode();
}

- (void)setCurrentWindowScalingMode:(XeniaIOSWindowScalingMode)mode {
  XeniaIOSSetCurrentWindowScalingMode(mode);
  [self.view setNeedsLayout];
  [self.view layoutIfNeeded];
}

- (BOOL)isGuestDisplayUncapped {
  if (!self.appContext) {
    return NO;
  }
  return !self.appContext->GetGuestDisplayRefreshCap();
}

- (void)setGuestDisplayUncapped:(BOOL)uncapped {
  if (!self.appContext) {
    return;
  }
  self.appContext->SetGuestDisplayRefreshCap(!uncapped);
}

- (BOOL)isPresentLetterboxEnabled {
  if (!self.appContext) {
    return cvars::present_letterbox;
  }
  return self.appContext->GetPresentLetterbox();
}

- (void)setPresentLetterboxEnabled:(BOOL)enabled {
  if (!self.appContext) {
    cvars::present_letterbox = enabled;
    return;
  }
  self.appContext->SetPresentLetterbox(enabled);
}

- (CGFloat)currentGuestDisplayAspectRatio {
  std::pair<uint32_t, uint32_t> aspect = {16, 9};
  if (self.appContext) {
    aspect = self.appContext->GetGuestDisplayAspectRatio();
  }
  if (!aspect.first || !aspect.second) {
    return 16.0 / 9.0;
  }
  const CGFloat aspect_ratio =
      static_cast<CGFloat>(aspect.first) / static_cast<CGFloat>(aspect.second);
  return std::isfinite(static_cast<double>(aspect_ratio)) && aspect_ratio > 0.0 ? aspect_ratio
                                                                                : 16.0 / 9.0;
}

- (void)applyMetalViewLayout {
  if (!self.metalView) {
    return;
  }
  const CGRect parent = self.view.bounds;
  if (CGRectIsEmpty(parent)) {
    return;
  }
  const CGFloat guest_aspect = [self currentGuestDisplayAspectRatio];
  const CGFloat parent_aspect = parent.size.width / MAX(parent.size.height, 1.0);
  const XeniaIOSWindowScalingMode mode = [self currentWindowScalingMode];
  self.metalView.xeniaDrawableAspectRatio =
      mode == XeniaIOSWindowScalingModeStretch ? parent_aspect : guest_aspect;
  CGRect frame = XeniaIOSMetalViewFrameForParent(parent, guest_aspect, mode,
                                                 XeniaIOSCurrentPortraitWindowOffset());

  if (!CGRectEqualToRect(self.metalView.frame, frame)) {
    self.metalView.frame = frame;
  }
}

- (UIMenu*)buildInGameDisplayMenu {
  __unsafe_unretained XeniaViewController* unsafe_self = self;
  const XeniaIOSWindowScalingMode current_mode = [self currentWindowScalingMode];

  UIAction* fit_action =
      [UIAction actionWithTitle:@"Fit (Preserve Aspect)"
                          image:[UIImage systemImageNamed:@"rectangle.center.inset.filled"]
                     identifier:nil
                        handler:^(__unused UIAction* action) {
                          [unsafe_self setCurrentWindowScalingMode:XeniaIOSWindowScalingModeFit];
                          [unsafe_self refreshInGameDisplayMenu];
                        }];
  fit_action.state =
      current_mode == XeniaIOSWindowScalingModeFit ? UIMenuElementStateOn : UIMenuElementStateOff;

  UIAction* stretch_action = [UIAction
      actionWithTitle:@"Stretch (Fill, Ignore Aspect)"
                image:[UIImage systemImageNamed:@"arrow.up.left.and.arrow.down.right"]
           identifier:nil
              handler:^(__unused UIAction* action) {
                [unsafe_self setCurrentWindowScalingMode:XeniaIOSWindowScalingModeStretch];
                [unsafe_self refreshInGameDisplayMenu];
              }];
  stretch_action.state = current_mode == XeniaIOSWindowScalingModeStretch ? UIMenuElementStateOn
                                                                          : UIMenuElementStateOff;

  UIAction* zoom_action =
      [UIAction actionWithTitle:@"Zoom (Fill, Crop)"
                          image:[UIImage systemImageNamed:@"rectangle.fill"]
                     identifier:nil
                        handler:^(__unused UIAction* action) {
                          [unsafe_self setCurrentWindowScalingMode:XeniaIOSWindowScalingModeZoom];
                          [unsafe_self refreshInGameDisplayMenu];
                        }];
  zoom_action.state =
      current_mode == XeniaIOSWindowScalingModeZoom ? UIMenuElementStateOn : UIMenuElementStateOff;

  UIMenu* scaling_submenu = [UIMenu menuWithTitle:@"Scaling"
                                            image:nil
                                       identifier:nil
                                          options:UIMenuOptionsDisplayInline
                                         children:@[ fit_action, stretch_action, zoom_action ]];

  const BOOL letterbox_enabled = [self isPresentLetterboxEnabled];
  UIAction* letterbox_action =
      [UIAction actionWithTitle:@"Letterbox"
                          image:[UIImage systemImageNamed:@"rectangle.center.inset.filled"]
                     identifier:nil
                        handler:^(__unused UIAction* action) {
                          const BOOL next_enabled = ![unsafe_self isPresentLetterboxEnabled];
                          [unsafe_self setPresentLetterboxEnabled:next_enabled];
                          [unsafe_self refreshInGameDisplayMenu];
                        }];
  letterbox_action.state = letterbox_enabled ? UIMenuElementStateOn : UIMenuElementStateOff;

  UIMenu* presentation_submenu = [UIMenu menuWithTitle:@"Presentation"
                                                 image:nil
                                            identifier:nil
                                               options:UIMenuOptionsDisplayInline
                                              children:@[ letterbox_action ]];

  const BOOL display_uncapped = [self isGuestDisplayUncapped];
  UIAction* uncapped_action =
      [UIAction actionWithTitle:@"Emulated Display Uncapped"
                          image:[UIImage systemImageNamed:@"speedometer"]
                     identifier:nil
                        handler:^(__unused UIAction* action) {
                          const BOOL next_uncapped = ![unsafe_self isGuestDisplayUncapped];
                          [unsafe_self setGuestDisplayUncapped:next_uncapped];
                          [unsafe_self refreshInGameDisplayMenu];
                        }];
  uncapped_action.state = display_uncapped ? UIMenuElementStateOn : UIMenuElementStateOff;

  UIMenu* refresh_submenu = [UIMenu menuWithTitle:@"Refresh Rate"
                                            image:nil
                                       identifier:nil
                                          options:UIMenuOptionsDisplayInline
                                         children:@[ uncapped_action ]];

  // Portrait position controls — only meaningful in portrait + Fit mode where
  // there's actual slack to drag the surface within.
  const BOOL is_portrait = self.view.bounds.size.height >= self.view.bounds.size.width;
  const BOOL position_available = is_portrait && current_mode == XeniaIOSWindowScalingModeFit;
  UIAction* position_action = [UIAction actionWithTitle:@"Position in Portrait…"
                                                  image:[UIImage systemImageNamed:@"hand.draw"]
                                             identifier:nil
                                                handler:^(__unused UIAction* action) {
                                                  [unsafe_self enterWindowPositionMode];
                                                }];
  if (!position_available) {
    position_action.attributes = UIMenuElementAttributesDisabled;
  }

  UIAction* reset_action =
      [UIAction actionWithTitle:@"Reset Position"
                          image:[UIImage systemImageNamed:@"arrow.counterclockwise"]
                     identifier:nil
                        handler:^(__unused UIAction* action) {
                          [unsafe_self resetWindowPosition];
                        }];

  UIMenu* position_submenu = [UIMenu menuWithTitle:@"Position"
                                             image:nil
                                        identifier:nil
                                           options:UIMenuOptionsDisplayInline
                                          children:@[ position_action, reset_action ]];

  return [UIMenu
      menuWithTitle:@"Display"
           children:@[ scaling_submenu, presentation_submenu, refresh_submenu, position_submenu ]];
}

#pragma mark - Window position drag mode (portrait, Fit only)

- (void)resetWindowPosition {
  XeniaIOSResetPortraitWindowOffset();
  [self.view setNeedsLayout];
  [self.view layoutIfNeeded];
}

- (void)enterWindowPositionMode {
  if (self.windowPositionOverlay) {
    return;
  }
  if (!self.gameRunning) {
    return;
  }
  [self hideInGameMenuOverlay];

  // Hide the touch overlay so its buttons don't compete for the pan gesture.
  // Will be restored on exit.
  if (self.touchControlsOverlay) {
    [self.touchControlsOverlay setGameplayOverlayVisible:NO animated:NO];
  }

  XeniaIOSWindowPositionOverlay* overlay =
      [[XeniaIOSWindowPositionOverlay alloc] initWithFrame:self.view.bounds];
  self.windowPositionOverlay = overlay;
  [overlay release];

  __unsafe_unretained XeniaViewController* unsafe_self = self;
  [self.windowPositionOverlay beginInView:self.view
                                metalView:self.metalView
                               completion:^{
                                 unsafe_self.windowPositionOverlay = nil;
                                 [unsafe_self updateTouchControlsOverlayVisibilityAnimated:YES];
                               }];
}

- (void)exitWindowPositionMode {
  if (!self.windowPositionOverlay) {
    return;
  }
  [self.windowPositionOverlay endAnimated:NO];
}

- (void)openGameTapped:(UIButton*)sender {
  (void)sender;
  [self.documentImportCoordinator presentGameImportPicker];
}

- (void)openSettingsTapped:(UIButton*)sender {
  (void)sender;
  [self presentSettingsDestinationWithInitialSection:XeniaSettingsInitialSectionMain
                         preventInteractiveDismissal:YES];
}

- (void)presentationControllerDidDismiss:(UIPresentationController*)presentationController {
  (void)presentationController;
  gameplay_modal_presentation_pending_ = NO;
  [self updateTouchControlsOverlayVisibilityAnimated:YES];
}

#pragma mark - Document import coordinator host

- (UIViewController*)documentImportCoordinatorPresenter {
  return [self topPresentedControllerForModalPresentation] ?: self;
}

- (BOOL)documentImportCoordinatorGameStopInProgress {
  return self.gameStopInProgress;
}

- (BOOL)documentImportCoordinatorJITAcquired {
  return self.jitAcquired;
}

- (void)documentImportCoordinatorSetStatusText:(NSString*)text {
  if (text.length > 0) {
    [self showStatusToastForMessage:text];
  }
}

- (std::filesystem::path)documentImportCoordinatorImportGameAtURL:(NSURL*)sourceURL
                                                            error:(NSError**)error {
  return [self importGameIntoLibrary:sourceURL error:error];
}

- (void)documentImportCoordinatorRefreshImportedGames {
  [self refreshImportedGames];
}

- (void)documentImportCoordinatorLaunchGameAtPath:(const std::filesystem::path&)gamePath
                                      displayName:(NSString*)displayName {
  [self launchGameAtPath:gamePath displayName:displayName];
}

- (BOOL)documentImportCoordinatorCanInstallTitleUpdates {
  return self.appContext != nullptr;
}

- (BOOL)documentImportCoordinatorInstallTitleUpdateAtPath:(const std::filesystem::path&)path
                                                   status:(std::string*)statusOut
                                           notTitleUpdate:(bool*)notTitleUpdateOut {
  if (!self.appContext) {
    if (statusOut) {
      *statusOut = "Title update installation is unavailable.";
    }
    if (notTitleUpdateOut) {
      *notTitleUpdateOut = false;
    }
    return NO;
  }
  return self.appContext->InstallTitleUpdate(path.string(), statusOut, notTitleUpdateOut);
}

- (void)documentImportCoordinatorPresentAlertWithTitle:(NSString*)title message:(NSString*)message {
  XEPresentOKAlert(self, title, message);
}

- (void)documentImportCoordinatorImportTouchLayoutAtURL:(NSURL*)url {
  [self.touchLayoutCoordinator importLayoutAtURL:url];
}

- (void)documentImportCoordinatorTouchLayoutImportCancelled {
  if (gameplay_modal_presentation_pending_) {
    gameplay_modal_presentation_pending_ = NO;
    [self updateTouchControlsOverlayVisibilityAnimated:YES];
  }
}

#pragma mark - Status bar / home indicator

- (BOOL)prefersStatusBarHidden {
  return YES;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
  // Always permit portrait and landscape, including during gameplay.
  // The previous gameplay-only landscape lock conflicted with the window
  // scaling modes; the portrait drag-to-position UX is meaningless if the user
  // can never rotate the device to portrait after launching a game.
  return UIInterfaceOrientationMaskAll;
}

- (UIInterfaceOrientation)preferredInterfaceOrientationForPresentation {
  return xe_current_interface_orientation(self.view);
}

- (BOOL)shouldAutorotate {
  return YES;
}

- (BOOL)prefersHomeIndicatorAutoHidden {
  return YES;
}

- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures {
  return UIRectEdgeAll;
}

#pragma mark - Public API

- (void)showLauncherOverlay {
  if (touch_layout_edit_mode_active_) {
    [self saveTouchLayoutModelForTitleID:active_game_title_id_];
    [self setTouchLayoutEditModeActive:NO animated:NO];
  }
  [self saveTouchLayoutModelForTitleID:active_game_title_id_];
  active_game_title_id_ = 0;
  active_touch_layout_local_id_.clear();
  gameplay_modal_presentation_pending_ = NO;
  self.gameRunning = NO;
  self.gameStopInProgress = NO;
  [self hideInGameMenuOverlay];
  self.launcherOverlayView.hidden = NO;
  self.statusLabel.text = @"";
  [self refreshSignedInProfileUI];
  [self updateJITStatusIndicator];
  [self updateJITAvailabilityUI];
  [self refreshImportedGamesAsync];
  [self.controllerNavigationCoordinator refreshLauncherFocus];
  xe_request_current_orientation(self);
  [UIView animateWithDuration:0.3
                   animations:^{
                     self.launcherOverlayView.alpha = 1.0;
                   }];
  [self updateTouchControlsOverlayVisibilityAnimated:YES];
}

- (void)dealloc {
  [self.jitPollTimer invalidate];
  [self.controllerNavigationCoordinator invalidate];
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [compat_data_ release];
  [_controllerNavigationCoordinator release];
  [_documentImportCoordinator release];
  [_touchLayoutCoordinator release];
  [_windowPositionOverlay release];
  [_touchControlsOverlay release];
  [_achievementNotificationPresenter release];
  [_statusToastPresenter release];
  [super dealloc];
}

#pragma mark - Touch overlay state

- (xe::hid::touch::IOSTouchRuntimeModel*)touchRuntimeModel {
  return touch_runtime_model_.get();
}

- (uint32_t)titleIDForGamePath:(const std::filesystem::path&)game_path {
  for (const IOSDiscoveredGame& game : discovered_games_) {
    if (game.path == game_path && game.title_id) {
      return game.title_id;
    }
    for (const auto& disc : game.discs) {
      if (disc.path == game_path && game.title_id) {
        return game.title_id;
      }
    }
  }
  IOSDiscoveredGame discovered_game;
  if (BuildDiscoveredGameFromPath(game_path, &discovered_game)) {
    return discovered_game.title_id;
  }
  return 0;
}

- (BOOL)hasConnectedGameplayController {
  for (GCController* controller in GCController.controllers) {
    if (controller.extendedGamepad || controller.microGamepad) {
      return YES;
    }
  }
  return NO;
}

- (BOOL)shouldBlockGameplayInput {
  return self.gameRunning &&
         (touch_layout_edit_mode_active_ || gameplay_modal_presentation_pending_ ||
          !self.inGameMenuOverlay.hidden || self.presentedViewController != nil);
}

- (BOOL)shouldShowTouchControlsOverlay {
  if (touch_layout_edit_mode_active_) {
    return self.gameRunning && self.launcherOverlayView.hidden && self.inGameMenuOverlay.hidden &&
           self.presentedViewController == nil;
  }

  if (!cvars::ios_touch_overlay) {
    return NO;
  }

  return self.gameRunning && self.launcherOverlayView.hidden && self.inGameMenuOverlay.hidden &&
         self.presentedViewController == nil && !gameplay_modal_presentation_pending_ &&
         ![self hasConnectedGameplayController];
}

- (void)setTouchLayoutEditModeActive:(BOOL)active animated:(BOOL)animated {
  touch_layout_edit_mode_active_ = active;
  if (self.touchControlsOverlay) {
    [self.touchControlsOverlay setEditingControlsEnabled:active animated:animated];
  }
  [self updateTouchControlsOverlayVisibilityAnimated:animated];
}

- (void)finishTouchLayoutEditMode {
  [self saveTouchLayoutModelForTitleID:active_game_title_id_];
  [self setTouchLayoutEditModeActive:NO animated:YES];
}

- (void)updateTouchControlsOverlayVisibilityAnimated:(BOOL)animated {
  const BOOL overlay_visible = [self shouldShowTouchControlsOverlay];
  if (self.touchControlsOverlay) {
    [self.touchControlsOverlay setGameplayOverlayVisible:overlay_visible animated:animated];
  }
  if (self.appContext) {
    self.appContext->SetGameplayInputBlocked([self shouldBlockGameplayInput] ? true : false);
  }
  // The screen-wide tap recognizer used to toggle the in-game menu would
  // otherwise fire every time the user presses a touch overlay button (the
  // recognizer's cancelsTouchesInView is NO so the button still fires too).
  // While the overlay is the active input surface the user opens the menu via
  // its dedicated pause button; the tap-anywhere fallback only matters when
  // the overlay is hidden (e.g. hardware controller connected) or when the
  // in-game menu is up and a tap-outside should dismiss it.
  in_game_menu_tap_recognizer_.enabled = !overlay_visible;
}

- (void)showStatusToast:(NSString*)message style:(XeniaIOSStatusToastStyle)style {
  [self.statusToastPresenter presentMessage:message style:style inView:self.view];
}

- (void)showStatusToastForMessage:(NSString*)message {
  if (message.length == 0) {
    return;
  }
  NSString* lower = [message lowercaseString];
  XeniaIOSStatusToastStyle style = XeniaIOSStatusToastStyleInfo;
  if ([lower rangeOfString:@"failed"].location != NSNotFound ||
      [lower rangeOfString:@"unable"].location != NSNotFound ||
      [lower rangeOfString:@"missing"].location != NSNotFound ||
      [lower rangeOfString:@"not found"].location != NSNotFound ||
      [lower rangeOfString:@"unavailable"].location != NSNotFound ||
      [lower rangeOfString:@"could not"].location != NSNotFound ||
      [lower rangeOfString:@"rejected"].location != NSNotFound) {
    style = XeniaIOSStatusToastStyleError;
  } else if ([lower rangeOfString:@"signed in"].location != NSNotFound ||
             [lower rangeOfString:@"copied"].location != NSNotFound ||
             [lower rangeOfString:@"imported"].location != NSNotFound ||
             [lower rangeOfString:@"installed"].location != NSNotFound ||
             [lower rangeOfString:@"applied"].location != NSNotFound ||
             [lower rangeOfString:@"renamed"].location != NSNotFound ||
             [lower rangeOfString:@"deleted"].location != NSNotFound ||
             [lower rangeOfString:@"saved copy"].location != NSNotFound ||
             [lower rangeOfString:@"restored"].location != NSNotFound ||
             [lower rangeOfString:@"sent"].location != NSNotFound) {
    style = XeniaIOSStatusToastStyleSuccess;
  }
  [self showStatusToast:message style:style];
}

- (void)pauseButtonTapped:(id)__unused sender {
  if (self.launcherOverlayView.hidden == NO || !self.gameRunning || self.presentedViewController) {
    return;
  }

  [self showInGameMenuOverlayAnimated:YES];
}

#pragma mark - Touch layout UI coordinator host

- (UIViewController*)topPresentedControllerForModalPresentation {
  UIViewController* presenter = self;
  while (presenter.presentedViewController) {
    presenter = presenter.presentedViewController;
  }
  return presenter;
}

- (xe::hid::touch::IOSTouchRuntimeModel*)touchLayoutCoordinatorRuntimeModel {
  return touch_runtime_model_.get();
}

- (uint32_t)touchLayoutCoordinatorActiveTitleID {
  return active_game_title_id_;
}

- (std::string)touchLayoutCoordinatorActiveLocalID {
  return active_touch_layout_local_id_;
}

- (void)touchLayoutCoordinatorSetActiveLocalID:(const std::string&)localID {
  active_touch_layout_local_id_ = localID;
}

- (BOOL)touchLayoutCoordinatorGameRunning {
  return self.gameRunning;
}

- (BOOL)touchLayoutCoordinatorCanPresentPendingInstall {
  return self.view.window &&
         [UIApplication sharedApplication].applicationState == UIApplicationStateActive &&
         !self.presentedViewController;
}

- (UIViewController*)touchLayoutCoordinatorTopPresenter {
  return [self topPresentedControllerForModalPresentation];
}

- (void)touchLayoutCoordinatorSetGameplayModalPresentationPending:(BOOL)pending {
  gameplay_modal_presentation_pending_ = pending;
}

- (void)touchLayoutCoordinatorUpdateTouchOverlayVisibilityAnimated:(BOOL)animated {
  [self updateTouchControlsOverlayVisibilityAnimated:animated];
}

- (void)touchLayoutCoordinatorRefreshTouchOverlayLayoutModel {
  [self.touchControlsOverlay refreshLayoutModel];
}

- (BOOL)touchLayoutCoordinatorIsShowingLayoutLibrary {
  return self.touchControlsOverlay && self.touchControlsOverlay.isShowingLayoutLibrary;
}

- (void)touchLayoutCoordinatorShowLayoutLibraryWithItems:
            (NSArray<XeniaTouchLayoutLibraryItem*>*)items
                                    currentLayoutLocalID:(NSString*)currentLayoutLocalID {
  [self.touchControlsOverlay showLayoutLibraryWithItems:items
                                   currentLayoutLocalID:currentLayoutLocalID];
}

- (void)touchLayoutCoordinatorSetStatusText:(NSString*)text {
  [self showStatusToastForMessage:text];
}

- (void)touchLayoutCoordinatorPresentAlertWithTitle:(NSString*)title message:(NSString*)message {
  XEPresentOKAlert(self, title, message);
}

- (void)touchLayoutCoordinatorPresentKeyboardPromptWithTitle:(NSString*)title
                                                 description:(NSString*)description
                                                 defaultText:(NSString*)defaultText
                                                  completion:(void (^)(BOOL cancelled,
                                                                       NSString* text))completion {
  [self presentSystemKeyboardPromptWithTitle:title
                                 description:description
                                 defaultText:defaultText
                                  completion:completion];
}

- (void)touchLayoutCoordinatorOpenTouchLayoutFileImportPicker {
  UIViewController* presenter = [self topPresentedControllerForModalPresentation];
  [self.documentImportCoordinator presentTouchLayoutImportPickerFromViewController:presenter];
}

- (void)touchLayoutCoordinatorEvaluateAutomaticStikDebugJITHandoffIfNeeded {
  [self evaluateAutomaticStikDebugJITHandoffIfNeeded];
}

#pragma mark - Touch layout coordination

- (void)applyDefaultTouchLayoutModel {
  [self.touchLayoutCoordinator applyDefaultLayoutModel];
}

- (void)applyTouchLayoutModelForTitleID:(uint32_t)title_id {
  [self.touchLayoutCoordinator applyLayoutModelForTitleID:title_id];
}

- (void)saveTouchLayoutModelForTitleID:(uint32_t)title_id {
  [self.touchLayoutCoordinator saveCurrentLayoutForTitleID:title_id];
}

- (void)presentTouchLayoutLibrarySheet {
  [self.touchLayoutCoordinator presentLibrary];
}

- (void)presentRenameTouchLayoutSheet {
  [self.touchLayoutCoordinator presentRenameSheet];
}

- (void)presentDeleteTouchLayoutSheet {
  [self.touchLayoutCoordinator presentDeleteSheet];
}

- (void)saveCurrentTouchLayoutCopy {
  [self.touchLayoutCoordinator saveCurrentLayoutCopy];
}

- (void)importTouchLayoutFromFile {
  [self.touchLayoutCoordinator importFromFile];
}

- (void)exportCurrentTouchLayout {
  [self.touchLayoutCoordinator exportCurrentLayout];
}

- (void)resetToOfficialTouchLayoutPreset {
  [self.touchLayoutCoordinator resetToOfficialPreset];
}

- (BOOL)handleExternalTouchLayoutFileURL:(NSURL*)url {
  return [self.touchLayoutCoordinator handleExternalFileURL:url];
}

- (BOOL)handleExternalTouchLayoutSchemeURL:(NSURL*)url {
  return [self.touchLayoutCoordinator handleExternalSchemeURL:url];
}

- (void)presentPendingTouchLayoutInstallIfReady {
  [self.touchLayoutCoordinator presentPendingInstallIfReady];
}

@end
