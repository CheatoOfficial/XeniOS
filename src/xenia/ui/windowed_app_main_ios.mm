/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#import <GameController/GameController.h>
#import <MetalKit/MetalKit.h>
#import <PhotosUI/PhotosUI.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <TargetConditionals.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/config.h"
#include "xenia/hid/input.h"
#include "xenia/ui/apple_ui_flags.h"
#include "xenia/ui/apple_ui_navigation.h"
#include "xenia/ui/surface_ios.h"
#include "xenia/ui/windowed_app.h"
#include "xenia/ui/windowed_app_context_ios.h"
#include "xenia/vfs/devices/xcontent_container_device.h"
#include "xenia/vfs/iso_metadata.h"
#include "xenia/vfs/xex_metadata.h"
#include "xenia/xbox.h"

DECLARE_path(log_file);

// Forward declarations of the Objective-C classes.
@class XeniaAppDelegate;
@class XeniaSceneDelegate;
@class XeniaViewController;
@class XeniaMetalView;

extern "C" int csops(pid_t pid, unsigned int ops, void* useraddr, size_t usersize);

#ifndef CS_OPS_STATUS
#define CS_OPS_STATUS 0
#endif
#ifndef CS_DEBUGGED
#define CS_DEBUGGED 0x10000000
#endif

static NSString* const kXeniaDiscussionDidUpdateNotification =
    @"XeniaDiscussionDidUpdateNotification";

static BOOL xe_is_cs_debugged(void) {
  int flags = 0;
  return !csops(getpid(), CS_OPS_STATUS, &flags, sizeof(flags)) && (flags & CS_DEBUGGED);
}

static BOOL xe_can_mmap_exec_page(void) {
  long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) page_size = 16384;
  void* test =
      mmap(NULL, (size_t)page_size, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (test == MAP_FAILED) {
    return NO;
  }
  munmap(test, (size_t)page_size);
  return YES;
}

static int xe_ios_product_major_version(void) {
#if TARGET_OS_TV
  return -1;
#else
  size_t version_size = 0;
  if (sysctlbyname("kern.osproductversion", nullptr, &version_size, nullptr, 0) == 0 &&
      version_size > 0) {
    std::string version(version_size, '\0');
    if (sysctlbyname("kern.osproductversion", version.data(), &version_size, nullptr, 0) == 0 &&
        version_size > 0) {
      if (!version.empty() && version.back() == '\0') {
        version.pop_back();
      }
      int parsed_major = 0;
      size_t index = 0;
      while (index < version.size() && version[index] >= '0' && version[index] <= '9') {
        parsed_major = parsed_major * 10 + (version[index] - '0');
        ++index;
      }
      if (parsed_major > 0) {
        return parsed_major;
      }
    }
  }
  return -1;
#endif
}

static BOOL xe_ios_requires_debugger_broker(void) {
  const int major = xe_ios_product_major_version();
  return major >= 26;
}

static NSString* xe_jit_not_detected_guidance_message(void) {
  if (!xe_ios_requires_debugger_broker()) {
    return @"JIT is not available yet. On iOS 18 and below, use a signature-patched sideload "
           @"(no debugger needed). You can open Settings while waiting.";
  }
  return @"Enable JIT first (StikDebug, SideJITServer, or AltJIT). "
         @"You can open Settings while waiting.";
}

// ---------------------------------------------------------------------------
// JIT availability check -- tests whether executable memory can be mapped.
// On iOS versions before 26, signature-patched sideloads can use normal W^X
// flow without debugger attachment. iOS 26+ requires debugger/broker state.
// ---------------------------------------------------------------------------
static BOOL xe_check_jit_available(void) {
  if (xe_ios_requires_debugger_broker()) {
    return xe_is_cs_debugged() && xe_can_mmap_exec_page();
  }
  return xe_can_mmap_exec_page();
}

static std::filesystem::path xe_get_ios_documents_path() {
  @autoreleasepool {
    NSArray* paths =
        NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    if (paths.count > 0) {
      return std::filesystem::path([paths[0] UTF8String]);
    }
    return std::filesystem::path([NSHomeDirectory() UTF8String]) / "Documents";
  }
}

static void xe_request_orientation(UIViewController* view_controller,
                                   UIInterfaceOrientationMask mask,
                                   UIInterfaceOrientation orientation) {
  if (!view_controller) {
    return;
  }
#if !TARGET_OS_TV
  [view_controller setNeedsUpdateOfSupportedInterfaceOrientations];
  if (@available(iOS 16.0, *)) {
    UIWindowScene* scene = view_controller.view.window.windowScene;
    if (!scene) {
      for (UIScene* connected_scene in [UIApplication sharedApplication].connectedScenes) {
        if ([connected_scene isKindOfClass:[UIWindowScene class]]) {
          scene = (UIWindowScene*)connected_scene;
          break;
        }
      }
    }
    if (scene) {
      UIWindowSceneGeometryPreferencesIOS* preferences =
          [[UIWindowSceneGeometryPreferencesIOS alloc] initWithInterfaceOrientations:mask];
      [scene requestGeometryUpdateWithPreferences:preferences
                                     errorHandler:^(NSError* error) {
                                       XELOGW("iOS: Orientation geometry update failed: {}",
                                              [[error localizedDescription] UTF8String]);
                                     }];
    }
  }
  [[UIDevice currentDevice] setValue:@(orientation) forKey:@"orientation"];
  [UIViewController attemptRotationToDeviceOrientation];
#endif
}

static void xe_request_landscape_orientation(UIViewController* view_controller) {
  xe_request_orientation(view_controller, UIInterfaceOrientationMaskLandscape,
                         UIInterfaceOrientationLandscapeRight);
}

static void xe_request_portrait_orientation(UIViewController* view_controller) {
  xe_request_orientation(view_controller, UIInterfaceOrientationMaskPortrait,
                         UIInterfaceOrientationPortrait);
}

static NSURL* xe_first_open_url_context_url(
    NSSet<UIOpenURLContext*>* url_contexts) {
  if (!url_contexts || url_contexts.count == 0) {
    return nil;
  }
  UIOpenURLContext* context = [url_contexts anyObject];
  return context.URL;
}

// ---------------------------------------------------------------------------
// XeniaMetalView - a UIView backed by a CAMetalLayer.
// ---------------------------------------------------------------------------
@interface XeniaMetalView : UIView
@end

@implementation XeniaMetalView

+ (Class)layerClass {
  return [CAMetalLayer class];
}

@end

// ---------------------------------------------------------------------------
// XeniaTheme – Design tokens aligned with the xenios-website globals.css
// @theme block.  Dark-mode only (the iOS app has no light mode).
// Uses an ObjC class so ARC properly retains the cached UIColor objects.
// ---------------------------------------------------------------------------
@interface XeniaTheme : NSObject
// Backgrounds.
+ (UIColor*)bgPrimary;    // #09090b
+ (UIColor*)bgSurface;    // #18181b
+ (UIColor*)bgSurface2;   // #27272a
+ (UIColor*)bgSurface3;   // #3f3f46
// Text.
+ (UIColor*)textPrimary;  // #fafafa
+ (UIColor*)textSecondary; // #a1a1aa
+ (UIColor*)textMuted;    // #71717a
// Accent.
+ (UIColor*)accent;       // #34d399
+ (UIColor*)accentHover;  // #6ee7b7
+ (UIColor*)accentFg;     // #09090b (same as bgPrimary)
// Status.
+ (UIColor*)statusError;  // #f87171
+ (UIColor*)statusWarning; // #fbbf24
// Borders & overlays.
+ (UIColor*)border;       // white 6%
+ (UIColor*)borderHover;  // white 10%
+ (UIColor*)overlay;      // black 85%
+ (UIColor*)overlayLight; // black 58%
@end

@implementation XeniaTheme
// No static caching — this file is compiled under MRC (manual reference
// counting), so static locals would hold dangling pointers after the
// autorelease pool drains.  UIColor creation is cheap; callers retain.
+ (UIColor*)bgPrimary {
  return [UIColor colorWithRed:0x09/255.0 green:0x09/255.0 blue:0x0b/255.0 alpha:1.0];
}
+ (UIColor*)bgSurface {
  return [UIColor colorWithRed:0x18/255.0 green:0x18/255.0 blue:0x1b/255.0 alpha:1.0];
}
+ (UIColor*)bgSurface2 {
  return [UIColor colorWithRed:0x27/255.0 green:0x27/255.0 blue:0x2a/255.0 alpha:1.0];
}
+ (UIColor*)bgSurface3 {
  return [UIColor colorWithRed:0x3f/255.0 green:0x3f/255.0 blue:0x46/255.0 alpha:1.0];
}
+ (UIColor*)textPrimary {
  return [UIColor colorWithRed:0xfa/255.0 green:0xfa/255.0 blue:0xfa/255.0 alpha:1.0];
}
+ (UIColor*)textSecondary {
  return [UIColor colorWithRed:0xa1/255.0 green:0xa1/255.0 blue:0xaa/255.0 alpha:1.0];
}
+ (UIColor*)textMuted {
  return [UIColor colorWithRed:0x71/255.0 green:0x71/255.0 blue:0x7a/255.0 alpha:1.0];
}
+ (UIColor*)accent {
  return [UIColor colorWithRed:0x34/255.0 green:0xd3/255.0 blue:0x99/255.0 alpha:1.0];
}
+ (UIColor*)accentHover {
  return [UIColor colorWithRed:0x6e/255.0 green:0xe7/255.0 blue:0xb7/255.0 alpha:1.0];
}
+ (UIColor*)accentFg {
  return [UIColor colorWithRed:0x09/255.0 green:0x09/255.0 blue:0x0b/255.0 alpha:1.0];
}
+ (UIColor*)statusError {
  return [UIColor colorWithRed:0xf8/255.0 green:0x71/255.0 blue:0x71/255.0 alpha:1.0];
}
+ (UIColor*)statusWarning {
  return [UIColor colorWithRed:0xfb/255.0 green:0xbf/255.0 blue:0x24/255.0 alpha:1.0];
}
+ (UIColor*)border {
  return [UIColor colorWithWhite:1.0 alpha:0.06];
}
+ (UIColor*)borderHover {
  return [UIColor colorWithWhite:1.0 alpha:0.10];
}
+ (UIColor*)overlay {
  return [UIColor colorWithWhite:0.0 alpha:0.85];
}
+ (UIColor*)overlayLight {
  return [UIColor colorWithWhite:0.0 alpha:0.58];
}
@end

@interface XeniaPaddedLabel : UILabel
@property(nonatomic) UIEdgeInsets padding;
@end

@implementation XeniaPaddedLabel

- (CGSize)intrinsicContentSize {
  CGSize size = [super intrinsicContentSize];
  return CGSizeMake(size.width + _padding.left + _padding.right,
                    size.height + _padding.top + _padding.bottom);
}

- (void)drawTextInRect:(CGRect)rect {
  [super drawTextInRect:UIEdgeInsetsInsetRect(rect, _padding)];
}

- (CGRect)textRectForBounds:(CGRect)bounds
     limitedToNumberOfLines:(NSInteger)numberOfLines {
  CGRect inset = UIEdgeInsetsInsetRect(bounds, _padding);
  CGRect result = [super textRectForBounds:inset
                    limitedToNumberOfLines:numberOfLines];
  result.origin.x -= _padding.left;
  result.origin.y -= _padding.top;
  result.size.width += _padding.left + _padding.right;
  result.size.height += _padding.top + _padding.bottom;
  return result;
}

@end

// Border radii matching website's Tailwind scale.
static constexpr CGFloat XeniaRadiusMd = 8.0;
static constexpr CGFloat XeniaRadiusLg = 12.0;
static constexpr CGFloat XeniaRadiusXl = 16.0;

namespace {

enum class IOSConfigControlType {
  kToggle,
  kChoiceInt32,
  kChoiceUInt64,
  kAction,
};

enum class IOSConfigAction {
  kNone,
  kViewRecentLog,
};

struct IOSConfigChoice {
  std::string title;
  int64_t value = 0;
};

struct IOSConfigItem {
  std::string key;
  std::string title;
  std::string subtitle;
  IOSConfigControlType control_type = IOSConfigControlType::kToggle;
  bool bool_value = false;
  int64_t choice_value = 0;
  IOSConfigAction action = IOSConfigAction::kNone;
  std::vector<IOSConfigChoice> choices;
};

struct IOSConfigSection {
  std::string title;
  std::string footer;
  std::vector<IOSConfigItem> items;
};

using IOSFocusNodeId = xe::ui::apple::FocusNodeId;
static constexpr IOSFocusNodeId kLauncherFocusSettings = 1;
static constexpr IOSFocusNodeId kLauncherFocusProfile = 2;
static constexpr IOSFocusNodeId kLauncherFocusImport = 3;
static constexpr IOSFocusNodeId kLauncherFocusLibrary = 4;
static constexpr IOSFocusNodeId kInGameFocusResume = 101;
static constexpr IOSFocusNodeId kInGameFocusSettings = 102;
static constexpr IOSFocusNodeId kInGameFocusLog = 103;
static constexpr IOSFocusNodeId kInGameFocusExit = 104;

uint64_t GetNowMs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

int16_t ToThumbAxis(float value) {
  const float clamped = std::clamp(value, -1.0f, 1.0f);
  return static_cast<int16_t>(clamped * 32767.0f);
}

uint8_t ToTriggerAxis(float value) {
  const float clamped = std::clamp(value, 0.0f, 1.0f);
  return static_cast<uint8_t>(clamped * 255.0f);
}

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

NSString* ToNSString(const std::string& value) {
  return [NSString stringWithUTF8String:value.c_str()];
}

NSString* DecodeURLComponent(NSString* value) {
  if (!value || value.length == 0) {
    return nil;
  }
  NSString* decoded = [value stringByRemovingPercentEncoding];
  if (decoded && decoded.length > 0) {
    return decoded;
  }
  return value;
}

bool BuildLaunchPathFromURLValue(NSString* value,
                                 std::filesystem::path* path_out) {
  if (!path_out) {
    return false;
  }
  NSString* normalized = DecodeURLComponent(value);
  if (!normalized || normalized.length == 0) {
    return false;
  }

  NSURL* nested_url = [NSURL URLWithString:normalized];
  if (nested_url && nested_url.isFileURL) {
    normalized = nested_url.path;
  }
  if (!normalized || normalized.length == 0 ||
      [normalized isEqualToString:@"/"]) {
    return false;
  }

  if ([normalized hasPrefix:@"private/"]) {
    normalized = [@"/" stringByAppendingString:normalized];
  } else if (![normalized hasPrefix:@"/"]) {
    normalized = [ToNSString(xe_get_ios_documents_path().string())
        stringByAppendingPathComponent:normalized];
  }

  *path_out = std::filesystem::path([normalized UTF8String]).lexically_normal();
  return !path_out->empty();
}

bool ExtractLaunchPathFromExternalURL(NSURL* url,
                                      std::filesystem::path* path_out) {
  if (!url || !path_out) {
    return false;
  }

  if (url.isFileURL) {
    const char* url_path = [url.path UTF8String];
    if (url_path && url_path[0]) {
      *path_out = std::filesystem::path(url_path).lexically_normal();
      return true;
    }
  }

  NSURLComponents* components =
      [NSURLComponents componentsWithURL:url resolvingAgainstBaseURL:NO];
  if (components) {
    NSArray<NSURLQueryItem*>* query_items = components.queryItems;
    NSArray<NSString*>* candidate_keys =
        @[ @"path", @"file", @"game", @"rom", @"url" ];
    for (NSString* key in candidate_keys) {
      for (NSURLQueryItem* item in query_items) {
        if (!item.name ||
            [item.name caseInsensitiveCompare:key] != NSOrderedSame) {
          continue;
        }
        if (BuildLaunchPathFromURLValue(item.value, path_out)) {
          return true;
        }
      }
    }
  }

  if (BuildLaunchPathFromURLValue(url.path, path_out)) {
    return true;
  }

  BOOL host_looks_like_path = NO;
  if (url.host && url.host.length > 0) {
    host_looks_like_path = [url.host hasPrefix:@"/"] ||
                           [url.host hasPrefix:@"private/"] ||
                           [url.host hasPrefix:@"%2F"] ||
                           [url.host hasPrefix:@"%2f"];
  }
  if (host_looks_like_path &&
      (!url.path || url.path.length == 0 ||
       [url.path isEqualToString:@"/"]) &&
      BuildLaunchPathFromURLValue(url.host, path_out)) {
    return true;
  }

  return false;
}

cvar::IConfigVar* GetConfigVar(const std::string& key) {
  if (!cvar::ConfigVars) {
    return nullptr;
  }
  auto it = cvar::ConfigVars->find(key);
  if (it == cvar::ConfigVars->end()) {
    return nullptr;
  }
  return it->second;
}

bool HasConfigVar(const std::string& key) { return GetConfigVar(key) != nullptr; }

std::string GetConfigVarString(const std::string& key, const std::string& fallback) {
  cvar::IConfigVar* var = GetConfigVar(key);
  if (!var) {
    return fallback;
  }
  return TrimAscii(var->config_value());
}

bool ParseBoolString(const std::string& text, bool* value_out) {
  if (!value_out) {
    return false;
  }
  std::string lower = text;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (lower == "true" || lower == "1") {
    *value_out = true;
    return true;
  }
  if (lower == "false" || lower == "0") {
    *value_out = false;
    return true;
  }
  return false;
}

bool ParseInt64String(const std::string& text, int64_t* value_out) {
  if (!value_out) {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  long long parsed = std::strtoll(text.c_str(), &end, 10);
  if (errno != 0 || !end || *end != '\0') {
    return false;
  }
  *value_out = static_cast<int64_t>(parsed);
  return true;
}

bool SetConfigVarBool(const std::string& key, bool value) {
  cvar::IConfigVar* var = GetConfigVar(key);
  if (!var) {
    XELOGW("iOS settings: missing config var '{}'", key);
    return false;
  }
  toml::value node(value);
  var->LoadConfigValue(&node);
  return true;
}

bool SetConfigVarInt32(const std::string& key, int32_t value) {
  cvar::IConfigVar* var = GetConfigVar(key);
  if (!var) {
    XELOGW("iOS settings: missing config var '{}'", key);
    return false;
  }
  toml::value node(value);
  var->LoadConfigValue(&node);
  return true;
}

bool SetConfigVarUInt64(const std::string& key, uint64_t value) {
  cvar::IConfigVar* var = GetConfigVar(key);
  if (!var) {
    XELOGW("iOS settings: missing config var '{}'", key);
    return false;
  }
  toml::value node(value);
  var->LoadConfigValue(&node);
  return true;
}

void AddBoolSetting(std::vector<IOSConfigItem>& items, const std::string& key,
                    const std::string& title, const std::string& subtitle, bool fallback) {
  if (!HasConfigVar(key)) {
    return;
  }
  IOSConfigItem item;
  item.key = key;
  item.title = title;
  item.subtitle = subtitle;
  item.control_type = IOSConfigControlType::kToggle;
  item.bool_value = fallback;
  ParseBoolString(GetConfigVarString(key, fallback ? "true" : "false"), &item.bool_value);
  items.push_back(std::move(item));
}

void AddChoiceSetting(std::vector<IOSConfigItem>& items, IOSConfigControlType control_type,
                      const std::string& key, const std::string& title, const std::string& subtitle,
                      int64_t fallback, std::vector<IOSConfigChoice> choices) {
  if (!HasConfigVar(key) || choices.empty()) {
    return;
  }
  IOSConfigItem item;
  item.key = key;
  item.title = title;
  item.subtitle = subtitle;
  item.control_type = control_type;
  item.choice_value = fallback;
  ParseInt64String(GetConfigVarString(key, std::to_string(fallback)), &item.choice_value);
  item.choices = std::move(choices);
  bool found = false;
  for (const IOSConfigChoice& choice : item.choices) {
    if (choice.value == item.choice_value) {
      found = true;
      break;
    }
  }
  if (!found) {
    item.choice_value = item.choices.front().value;
  }
  items.push_back(std::move(item));
}

void AddActionSetting(std::vector<IOSConfigItem>& items, IOSConfigAction action,
                      const std::string& title, const std::string& subtitle) {
  IOSConfigItem item;
  item.title = title;
  item.subtitle = subtitle;
  item.control_type = IOSConfigControlType::kAction;
  item.action = action;
  items.push_back(std::move(item));
}

std::string ChoiceTitleForItem(const IOSConfigItem& item) {
  for (const IOSConfigChoice& choice : item.choices) {
    if (choice.value == item.choice_value) {
      return choice.title;
    }
  }
  return item.choices.empty() ? std::string() : item.choices.front().title;
}

std::filesystem::path GetLogFilePath() {
  if (!cvars::log_file.empty()) {
    return cvars::log_file;
  }
  return xe_get_ios_documents_path() / "xenia.log";
}

std::string ReadFileTail(const std::filesystem::path& path, size_t max_bytes) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return std::string();
  }

  file.seekg(0, std::ios::end);
  const std::streamoff end = file.tellg();
  if (end <= 0) {
    return std::string();
  }

  const std::streamoff max_read = static_cast<std::streamoff>(max_bytes);
  const std::streamoff start = end > max_read ? (end - max_read) : 0;
  file.seekg(start, std::ios::beg);

  std::string content(static_cast<size_t>(end - start), '\0');
  file.read(content.data(), static_cast<std::streamsize>(content.size()));
  content.resize(static_cast<size_t>(file.gcount()));

  if (start > 0) {
    size_t first_newline = content.find('\n');
    if (first_newline != std::string::npos) {
      content.erase(0, first_newline + 1);
    }
  }
  return content;
}

NSString* DecodeLogBytesToNSString(const std::string& content) {
  if (content.empty()) {
    return @"";
  }

  NSData* log_data = [NSData dataWithBytes:content.data() length:content.size()];
  NSString* decoded = [[NSString alloc] initWithData:log_data encoding:NSUTF8StringEncoding];
  if (!decoded) {
    decoded = [[NSString alloc] initWithData:log_data
                                    encoding:CFStringConvertEncodingToNSStringEncoding(
                                                 kCFStringEncodingWindowsLatin1)];
  }
  if (!decoded) {
    decoded = [[NSString alloc] initWithData:log_data encoding:NSISOLatin1StringEncoding];
  }
  if (decoded) {
    return decoded;
  }

  // Last-resort byte-preserving fallback so the UI never shows a hard decode
  // failure marker.
  NSMutableString* fallback = [NSMutableString stringWithCapacity:content.size()];
  for (unsigned char byte : content) {
    if (byte == '\n' || byte == '\r' || byte == '\t' ||
        (byte >= 0x20 && byte <= 0x7E)) {
      [fallback appendFormat:@"%c", byte];
    } else {
      [fallback appendFormat:@"\\x%02X", byte];
    }
  }
  return fallback;
}

struct IOSDiscoveredGame {
  std::filesystem::path path;
  std::string title;
  uint32_t title_id = 0;
  std::vector<uint8_t> icon_data;
  bool has_compat_info = false;
  std::string compat_status;
  std::string compat_perf;
  std::string compat_notes;
  bool has_installed_content = false;
};

std::string ToLowerAsciiCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return value;
}

bool IsISOPath(const std::filesystem::path& path) {
  return ToLowerAsciiCopy(path.extension().string()) == ".iso";
}

bool IsDefaultXexPath(const std::filesystem::path& path) {
  return ToLowerAsciiCopy(path.filename().string()) == "default.xex";
}

bool IsLikelyGodPath(const std::filesystem::path& path) {
  if (!path.has_filename()) {
    return false;
  }
  std::filesystem::path parent = path.parent_path();
  while (!parent.empty()) {
    std::string name_lower = ToLowerAsciiCopy(parent.filename().string());
    if (name_lower == "00007000" || name_lower == "00004000") {
      return true;
    }
    std::filesystem::path next = parent.parent_path();
    if (next == parent) {
      break;
    }
    parent = next;
  }
  return false;
}

bool LooksLikeHexContentFilename(const std::filesystem::path& path) {
  const std::string filename = path.filename().string();
  if (filename.size() < 24 || filename.size() > 40) {
    return false;
  }
  return std::all_of(filename.begin(), filename.end(), [](unsigned char c) {
    return std::isxdigit(c) != 0;
  });
}

bool IsLikelyGodContainerFile(const std::filesystem::path& path) {
  if (IsLikelyGodPath(path)) {
    return true;
  }
  if (path.has_extension()) {
    return false;
  }
  return LooksLikeHexContentFilename(path);
}

bool HasContentSidecarDataDirectory(const std::filesystem::path& path) {
  std::filesystem::path sidecar_path = path;
  sidecar_path += ".data";
  std::error_code ec;
  return std::filesystem::is_directory(sidecar_path, ec) && !ec;
}

std::string FormatTitleID(uint32_t title_id) {
  if (!title_id) {
    return std::string();
  }
  char buffer[9] = {};
  std::snprintf(buffer, sizeof(buffer), "%08X", title_id);
  return std::string(buffer);
}

enum class IOSInstalledContentKind {
  kTitleUpdate,
  kDlc,
};

struct IOSInstalledContentEntry {
  IOSInstalledContentKind kind = IOSInstalledContentKind::kTitleUpdate;
  std::string name;
  std::filesystem::path path;
};

struct IOSSelectedContentPackage {
  uint32_t title_id = 0;
  xe::XContentType content_type = xe::XContentType::kSavedGame;
  std::filesystem::path path;
};

static std::filesystem::path xe_title_content_root(uint32_t title_id) {
  char title_id_buffer[9] = {};
  std::snprintf(title_id_buffer, sizeof(title_id_buffer), "%08X", title_id);
  return xe_get_ios_documents_path() / "content" / "0000000000000000" / title_id_buffer;
}

static std::filesystem::path xe_title_update_content_root(uint32_t title_id) {
  return xe_title_content_root(title_id) / "000B0000";
}

static std::filesystem::path xe_dlc_content_root(uint32_t title_id) {
  return xe_title_content_root(title_id) / "00000002";
}

static NSString* xe_installed_content_kind_label(IOSInstalledContentKind kind) {
  switch (kind) {
    case IOSInstalledContentKind::kTitleUpdate:
      return @"Title Update";
    case IOSInstalledContentKind::kDlc:
      return @"DLC";
  }
  return @"Content";
}

static void xe_present_ok_alert(UIViewController* presenter, NSString* title, NSString* message) {
  if (!presenter) {
    return;
  }
  UIAlertController* alert =
      [UIAlertController alertControllerWithTitle:title ?: @"Notice"
                                          message:message ?: @""
                                   preferredStyle:UIAlertControllerStyleAlert];
  [alert addAction:[UIAlertAction actionWithTitle:@"OK"
                                            style:UIAlertActionStyleCancel
                                          handler:nil]];
  [presenter presentViewController:alert animated:YES completion:nil];
}

static std::string xe_content_package_directory_name(const std::filesystem::path& package_path) {
  std::string name = package_path.stem().string();
  if (name.empty()) {
    name = package_path.filename().string();
  }
  return name;
}

static bool xe_read_selected_content_package(const std::filesystem::path& path,
                                             IOSSelectedContentPackage* package_out,
                                             NSString** error_message_out) {
  if (error_message_out) {
    *error_message_out = nil;
  }

  auto header = xe::vfs::XContentContainerDevice::ReadContainerHeader(path);
  if (!header || !header->content_header.is_magic_valid()) {
    if (error_message_out) {
      *error_message_out = @"Could not read the content package header.";
    }
    return false;
  }

  if (header->content_metadata.data_file_count > 0 && !HasContentSidecarDataDirectory(path)) {
    if (error_message_out) {
      *error_message_out = @"This content package is missing its required .data sidecar folder.";
    }
    return false;
  }

  if (package_out) {
    package_out->title_id = header->content_metadata.execution_info.title_id;
    package_out->content_type =
        static_cast<xe::XContentType>(header->content_metadata.content_type.get());
    package_out->path = path;
  }
  return true;
}

static bool xe_copy_directory_recursive(const std::filesystem::path& source,
                                        const std::filesystem::path& destination,
                                        std::string* error_message_out) {
  std::error_code ec;
  std::filesystem::remove_all(destination, ec);
  ec.clear();
  std::filesystem::copy(
      source, destination,
      std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
      ec);
  if (ec) {
    if (error_message_out) {
      *error_message_out = ec.message();
    }
    return false;
  }
  return true;
}

static bool xe_copy_content_package_into_root(const IOSSelectedContentPackage& package_info,
                                              const std::filesystem::path& destination_root,
                                              std::string* error_message_out) {
  if (error_message_out) {
    *error_message_out = "";
  }

  const std::filesystem::path package_directory =
      destination_root / xe_content_package_directory_name(package_info.path);
  std::error_code ec;
  std::filesystem::create_directories(package_directory, ec);
  if (ec) {
    if (error_message_out) {
      *error_message_out = "Failed creating content folder: " + ec.message();
    }
    return false;
  }

  const std::filesystem::path destination_file = package_directory / package_info.path.filename();
  std::filesystem::copy_file(package_info.path, destination_file,
                             std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    if (error_message_out) {
      *error_message_out = "Failed copying package: " + ec.message();
    }
    return false;
  }

  if (HasContentSidecarDataDirectory(package_info.path)) {
    std::filesystem::path source_sidecar = package_info.path;
    source_sidecar += ".data";
    std::filesystem::path destination_sidecar = destination_file;
    destination_sidecar += ".data";
    if (!xe_copy_directory_recursive(source_sidecar, destination_sidecar, error_message_out)) {
      return false;
    }
  }

  return true;
}

static void xe_collect_installed_content(const std::filesystem::path& root,
                                         IOSInstalledContentKind kind,
                                         std::vector<IOSInstalledContentEntry>* content_out) {
  if (!content_out) {
    return;
  }

  std::error_code ec;
  if (!std::filesystem::exists(root, ec)) {
    return;
  }

  std::filesystem::directory_iterator it(root, ec);
  std::filesystem::directory_iterator end;
  for (; !ec && it != end; ++it) {
    if (!it->is_directory(ec) || ec) {
      ec.clear();
      continue;
    }

    IOSInstalledContentEntry entry;
    entry.kind = kind;
    entry.name = it->path().filename().string();
    entry.path = it->path();
    content_out->push_back(std::move(entry));
  }
}

static std::vector<IOSInstalledContentEntry> xe_list_installed_content(uint32_t title_id) {
  std::vector<IOSInstalledContentEntry> content;
  if (!title_id) {
    return content;
  }

  xe_collect_installed_content(xe_title_update_content_root(title_id),
                               IOSInstalledContentKind::kTitleUpdate, &content);
  xe_collect_installed_content(xe_dlc_content_root(title_id), IOSInstalledContentKind::kDlc,
                               &content);
  std::sort(content.begin(), content.end(),
            [](const IOSInstalledContentEntry& a, const IOSInstalledContentEntry& b) {
              if (a.kind != b.kind) {
                return a.kind < b.kind;
              }
              return a.name < b.name;
            });
  return content;
}

static NSString* xe_normalize_game_title_for_ui(NSString* title) {
  if (!title || title.length == 0) {
    return title;
  }
  if ([title rangeOfCharacterFromSet:[NSCharacterSet whitespaceCharacterSet]]
          .location != NSNotFound) {
    return title;
  }
  NSRange letter_range =
      [title rangeOfCharacterFromSet:[NSCharacterSet letterCharacterSet]];
  if (letter_range.location == NSNotFound) {
    return title;
  }
  NSRange lower_range =
      [title rangeOfCharacterFromSet:[NSCharacterSet lowercaseLetterCharacterSet]];
  if (lower_range.location != NSNotFound) {
    return title;
  }
  NSCharacterSet* roman_set =
      [NSCharacterSet characterSetWithCharactersInString:@"IVXLCDM"];
  NSCharacterSet* non_roman_set = [roman_set invertedSet];
  if ([title rangeOfCharacterFromSet:non_roman_set].location == NSNotFound) {
    return title;
  }
  return [title localizedCapitalizedString];
}

std::string NormalizeGameTitleForUI(const std::string& title) {
  NSString* normalized = xe_normalize_game_title_for_ui(ToNSString(title));
  return normalized ? std::string([normalized UTF8String]) : title;
}

static UIColor* xe_compat_status_color(NSString* status) {
  if ([status isEqualToString:@"playable"])
    return [UIColor colorWithRed:0x34 / 255.0
                           green:0xd3 / 255.0
                            blue:0x99 / 255.0
                           alpha:1.0];
  if ([status isEqualToString:@"ingame"])
    return [UIColor colorWithRed:0x60 / 255.0
                           green:0xa5 / 255.0
                            blue:0xfa / 255.0
                           alpha:1.0];
  if ([status isEqualToString:@"intro"])
    return [UIColor colorWithRed:0xfb / 255.0
                           green:0xbf / 255.0
                            blue:0x24 / 255.0
                           alpha:1.0];
  if ([status isEqualToString:@"loads"])
    return [UIColor colorWithRed:0xfb / 255.0
                           green:0x92 / 255.0
                            blue:0x3c / 255.0
                           alpha:1.0];
  if ([status isEqualToString:@"nothing"])
    return [UIColor colorWithRed:0xf8 / 255.0
                           green:0x71 / 255.0
                            blue:0x71 / 255.0
                           alpha:1.0];
  return [XeniaTheme textMuted];
}

static NSString* xe_compat_status_label(NSString* status) {
  if ([status isEqualToString:@"playable"]) return @"Playable";
  if ([status isEqualToString:@"ingame"]) return @"In-Game";
  if ([status isEqualToString:@"intro"]) return @"Intro";
  if ([status isEqualToString:@"loads"]) return @"Loads";
  if ([status isEqualToString:@"nothing"]) return @"Nothing";
  return @"Unknown";
}

static UIColor* xe_compat_perf_color(NSString* perf) {
  if ([perf isEqualToString:@"great"]) {
    return [XeniaTheme accent];
  }
  if ([perf isEqualToString:@"ok"]) {
    return [UIColor colorWithRed:0x60 / 255.0 green:0xa5 / 255.0 blue:0xfa / 255.0 alpha:1.0];
  }
  if ([perf isEqualToString:@"poor"]) {
    return [XeniaTheme statusWarning];
  }
  return [XeniaTheme textMuted];
}

static NSString* xe_compat_perf_label(NSString* perf) {
  if ([perf isEqualToString:@"great"]) return @"Great";
  if ([perf isEqualToString:@"ok"]) return @"Okay";
  if ([perf isEqualToString:@"poor"]) return @"Poor";
  if ([perf isEqualToString:@"n/a"]) return @"N/A";
  return @"Unknown";
}

static NSString* xe_format_iso_date(NSString* iso) {
  if (!iso || iso.length < 10) {
    return iso ?: @"";
  }
  static NSDateFormatter* parser = nil;
  static NSDateFormatter* date_only_parser = nil;
  static NSDateFormatter* formatter = nil;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    parser = [[NSDateFormatter alloc] init];
    parser.dateFormat = @"yyyy-MM-dd'T'HH:mm:ss";
    parser.locale = [[[NSLocale alloc] initWithLocaleIdentifier:@"en_US_POSIX"] autorelease];
    date_only_parser = [[NSDateFormatter alloc] init];
    date_only_parser.dateFormat = @"yyyy-MM-dd";
    date_only_parser.locale =
        [[[NSLocale alloc] initWithLocaleIdentifier:@"en_US_POSIX"] autorelease];
    formatter = [[NSDateFormatter alloc] init];
    formatter.dateStyle = NSDateFormatterMediumStyle;
    formatter.timeStyle = NSDateFormatterNoStyle;
  });

  NSString* trimmed = [iso substringToIndex:MIN(iso.length, (NSUInteger)19)];
  NSDate* date = [parser dateFromString:trimmed];
  if (!date) {
    date = [date_only_parser dateFromString:[iso substringToIndex:MIN(iso.length, (NSUInteger)10)]];
  }
  return date ? [formatter stringFromDate:date]
              : [iso substringToIndex:MIN(iso.length, (NSUInteger)10)];
}

static NSString* xe_platform_display_text(NSString* platform, NSString* os_version) {
  if (![platform isKindOfClass:[NSString class]] || platform.length == 0) {
    return @"";
  }
  NSString* platform_name = [platform isEqualToString:@"ios"]     ? @"iOS"
                            : [platform isEqualToString:@"macos"] ? @"macOS"
                                                                  : platform;
  if (![os_version isKindOfClass:[NSString class]] || os_version.length == 0) {
    return platform_name;
  }
  return [NSString stringWithFormat:@"%@ %@", platform_name, os_version];
}

static NSString* xe_discussion_cache_path(uint32_t title_id) {
  NSString* caches =
      NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject;
  return [caches
      stringByAppendingPathComponent:[NSString stringWithFormat:@"discussion-%08X.json", title_id]];
}

static UIFont* xe_scaled_system_font(UIFontTextStyle text_style,
                                     CGFloat point_size,
                                     UIFontWeight weight) {
  UIFont* base_font = [UIFont systemFontOfSize:point_size weight:weight];
  return [[UIFontMetrics metricsForTextStyle:text_style] scaledFontForFont:base_font];
}

static UIFont* xe_scaled_monospaced_font(UIFontTextStyle text_style,
                                         CGFloat point_size,
                                         UIFontWeight weight) {
  UIFont* base_font = [UIFont monospacedSystemFontOfSize:point_size weight:weight];
  return [[UIFontMetrics metricsForTextStyle:text_style] scaledFontForFont:base_font];
}

static void xe_apply_label_font(UILabel* label,
                                UIFontTextStyle text_style,
                                CGFloat point_size,
                                UIFontWeight weight) {
  if (!label) {
    return;
  }
  label.font = xe_scaled_system_font(text_style, point_size, weight);
  if (@available(iOS 10.0, *)) {
    label.adjustsFontForContentSizeCategory = YES;
  }
}

static void xe_apply_monospaced_label_font(UILabel* label,
                                           UIFontTextStyle text_style,
                                           CGFloat point_size,
                                           UIFontWeight weight) {
  if (!label) {
    return;
  }
  label.font = xe_scaled_monospaced_font(text_style, point_size, weight);
  if (@available(iOS 10.0, *)) {
    label.adjustsFontForContentSizeCategory = YES;
  }
}

static void xe_apply_button_title_font(UIButton* button,
                                       UIFontTextStyle text_style,
                                       CGFloat point_size,
                                       UIFontWeight weight) {
  if (!button.titleLabel) {
    return;
  }
  button.titleLabel.font = xe_scaled_system_font(text_style, point_size, weight);
  if (@available(iOS 10.0, *)) {
    button.titleLabel.adjustsFontForContentSizeCategory = YES;
  }
}

static void xe_apply_text_view_font(UITextView* text_view,
                                    UIFontTextStyle text_style,
                                    CGFloat point_size,
                                    UIFontWeight weight,
                                    BOOL monospaced) {
  if (!text_view) {
    return;
  }
  text_view.font = monospaced ? xe_scaled_monospaced_font(text_style, point_size, weight)
                              : xe_scaled_system_font(text_style, point_size, weight);
  if (@available(iOS 10.0, *)) {
    text_view.adjustsFontForContentSizeCategory = YES;
  }
}

static XeniaPaddedLabel* xe_make_tag_pill(NSString* text, UIColor* text_color) {
  XeniaPaddedLabel* pill = [[[XeniaPaddedLabel alloc] init] autorelease];
  pill.translatesAutoresizingMaskIntoConstraints = NO;
  pill.padding = UIEdgeInsetsMake(2, 8, 2, 8);
  xe_apply_label_font(pill, UIFontTextStyleCaption1, 12.0, UIFontWeightMedium);
  pill.text = text ?: @"";
  pill.textColor = text_color ?: [XeniaTheme textMuted];
  pill.backgroundColor = [(text_color ?: [XeniaTheme textMuted]) colorWithAlphaComponent:0.1];
  pill.layer.cornerRadius = 8.0;
  pill.clipsToBounds = YES;
  [pill setContentHuggingPriority:UILayoutPriorityRequired
                          forAxis:UILayoutConstraintAxisHorizontal];
  [pill setContentCompressionResistancePriority:UILayoutPriorityRequired
                                        forAxis:UILayoutConstraintAxisHorizontal];
  return pill;
}

static UIButton* xe_make_ios_sheet_close_button(id target, SEL action) {
  UIButton* button = [UIButton buttonWithType:UIButtonTypeSystem];
  button.translatesAutoresizingMaskIntoConstraints = NO;
  button.backgroundColor = [[XeniaTheme bgSurface] colorWithAlphaComponent:0.76];
  button.layer.cornerRadius = 24.0;
  button.layer.borderWidth = 0.5;
  button.layer.borderColor = [[UIColor whiteColor] colorWithAlphaComponent:0.12].CGColor;
  UIImageSymbolConfiguration* config =
      [UIImageSymbolConfiguration configurationWithPointSize:22
                                                     weight:UIImageSymbolWeightSemibold];
  UIImage* image =
      [[UIImage systemImageNamed:@"xmark.circle"] imageByApplyingSymbolConfiguration:config];
  [button setImage:image forState:UIControlStateNormal];
  button.tintColor = [XeniaTheme accent];
  [button addTarget:target action:action forControlEvents:UIControlEventTouchUpInside];
  [NSLayoutConstraint activateConstraints:@[
    [button.widthAnchor constraintEqualToConstant:48.0],
    [button.heightAnchor constraintEqualToConstant:48.0],
  ]];
  return button;
}

static NSArray<NSString*>* xe_compat_statuses(void) {
  return @[ @"playable", @"ingame", @"intro", @"loads", @"nothing" ];
}

static NSArray<NSString*>* xe_compat_status_labels(void) {
  return @[ @"Playable", @"In-Game", @"Intro", @"Loads", @"Nothing" ];
}

static NSArray<NSString*>* xe_compat_perfs(void) { return @[ @"great", @"ok", @"poor", @"n/a" ]; }

static NSArray<NSString*>* xe_compat_perf_labels(void) {
  return @[ @"Great", @"OK", @"Poor", @"N/A" ];
}

static CGFloat xe_compat_hero_height_for_width(CGFloat width) {
  CGFloat hero_width = MAX(width, 0.0);
  if (hero_width <= 0.0) {
    return 286.0;
  }
  return MIN(360.0, MAX(286.0, floor(hero_width * 0.74)));
}

static CGRect xe_compat_background_contents_rect(UIImage* image, CGSize bounds_size) {
  if (!image || image.size.width <= 0.0 || image.size.height <= 0.0 || bounds_size.width <= 0.0 ||
      bounds_size.height <= 0.0) {
    return CGRectMake(0.0, 0.0, 1.0, 1.0);
  }

  CGFloat image_aspect = image.size.width / image.size.height;
  CGFloat bounds_aspect = bounds_size.width / bounds_size.height;
  if (image_aspect <= bounds_aspect + 0.01) {
    return CGRectMake(0.0, 0.0, 1.0, 1.0);
  }

  CGFloat visible_width = bounds_aspect / image_aspect;
  visible_width = MIN(MAX(visible_width, 0.01), 1.0);
  return CGRectMake(0.0, 0.0, visible_width, 1.0);
}

static NSString* xe_device_machine(void) {
  size_t size = 0;
  sysctlbyname("hw.machine", nullptr, &size, nullptr, 0);
  if (size == 0) {
    return @"Unknown";
  }

  char* machine = static_cast<char*>(malloc(size));
  if (!machine) {
    return @"Unknown";
  }
  sysctlbyname("hw.machine", machine, &size, nullptr, 0);
  NSString* value = [NSString stringWithCString:machine encoding:NSUTF8StringEncoding];
  free(machine);
  return value ?: @"Unknown";
}

static NSString* xe_device_display_name_for_machine(NSString* raw_machine) {
  if (![raw_machine isKindOfClass:[NSString class]]) {
    return @"Unknown";
  }
  NSString* machine = [raw_machine
      stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
  if (machine.length == 0) {
    return @"Unknown";
  }
  static NSDictionary<NSString*, NSString*>* overrides = nil;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    NSMutableDictionary<NSString*, NSString*>* map = [NSMutableDictionary dictionary];
    void (^add_names)(NSArray<NSString*>*, NSString*) = ^(NSArray<NSString*>* codes, NSString* name) {
      for (NSString* code in codes) {
        map[code] = name;
      }
    };

    add_names(@[ @"iPhone13,1" ], @"iPhone 12 Mini");
    add_names(@[ @"iPhone13,2" ], @"iPhone 12");
    add_names(@[ @"iPhone13,3" ], @"iPhone 12 Pro");
    add_names(@[ @"iPhone13,4" ], @"iPhone 12 Pro Max");
    add_names(@[ @"iPhone14,2" ], @"iPhone 13 Pro");
    add_names(@[ @"iPhone14,3" ], @"iPhone 13 Pro Max");
    add_names(@[ @"iPhone14,4" ], @"iPhone 13 Mini");
    add_names(@[ @"iPhone14,5" ], @"iPhone 13");
    add_names(@[ @"iPhone14,6" ], @"iPhone SE 3rd Gen");
    add_names(@[ @"iPhone14,7" ], @"iPhone 14");
    add_names(@[ @"iPhone14,8" ], @"iPhone 14 Plus");
    add_names(@[ @"iPhone15,2" ], @"iPhone 14 Pro");
    add_names(@[ @"iPhone15,3" ], @"iPhone 14 Pro Max");
    add_names(@[ @"iPhone15,4" ], @"iPhone 15");
    add_names(@[ @"iPhone15,5" ], @"iPhone 15 Plus");
    add_names(@[ @"iPhone16,1" ], @"iPhone 15 Pro");
    add_names(@[ @"iPhone16,2" ], @"iPhone 15 Pro Max");
    add_names(@[ @"iPhone17,1" ], @"iPhone 16 Pro");
    add_names(@[ @"iPhone17,2" ], @"iPhone 16 Pro Max");
    add_names(@[ @"iPhone17,3" ], @"iPhone 16");
    add_names(@[ @"iPhone17,4" ], @"iPhone 16 Plus");
    add_names(@[ @"iPhone17,5" ], @"iPhone 16e");
    add_names(@[ @"iPhone18,1" ], @"iPhone 17 Pro");
    add_names(@[ @"iPhone18,2" ], @"iPhone 17 Pro Max");
    add_names(@[ @"iPhone18,3" ], @"iPhone 17");
    add_names(@[ @"iPhone18,4" ], @"iPhone Air");

    // Collapse WiFi and cellular iPad SKUs to a single product label.
    add_names(@[ @"iPad8,9", @"iPad8,10" ], @"iPad Pro 11 inch 4th Gen");
    add_names(@[ @"iPad8,11", @"iPad8,12" ], @"iPad Pro 12.9 inch 4th Gen");
    add_names(@[ @"iPad11,1", @"iPad11,2" ], @"iPad mini 5th Gen");
    add_names(@[ @"iPad11,3", @"iPad11,4" ], @"iPad Air 3rd Gen");
    add_names(@[ @"iPad11,6", @"iPad11,7" ], @"iPad 8th Gen");
    add_names(@[ @"iPad12,1", @"iPad12,2" ], @"iPad 9th Gen");
    add_names(@[ @"iPad13,1", @"iPad13,2" ], @"iPad Air 4th Gen");
    add_names(@[ @"iPad13,4", @"iPad13,5", @"iPad13,6", @"iPad13,7" ],
              @"iPad Pro 11 inch 5th Gen");
    add_names(@[ @"iPad13,8", @"iPad13,9", @"iPad13,10", @"iPad13,11" ],
              @"iPad Pro 12.9 inch 5th Gen");
    add_names(@[ @"iPad13,16", @"iPad13,17" ], @"iPad Air 5th Gen");
    add_names(@[ @"iPad13,18", @"iPad13,19" ], @"iPad 10th Gen");
    add_names(@[ @"iPad14,1", @"iPad14,2" ], @"iPad mini 6th Gen");
    add_names(@[ @"iPad14,3", @"iPad14,4" ], @"iPad Pro 11 inch 4th Gen");
    add_names(@[ @"iPad14,5", @"iPad14,6" ], @"iPad Pro 12.9 inch 6th Gen");
    add_names(@[ @"iPad14,8", @"iPad14,9" ], @"iPad Air 11 inch 6th Gen");
    add_names(@[ @"iPad14,10", @"iPad14,11" ], @"iPad Air 13 inch 6th Gen");
    add_names(@[ @"iPad15,3", @"iPad15,4" ], @"iPad Air 11-inch 7th Gen");
    add_names(@[ @"iPad15,5", @"iPad15,6" ], @"iPad Air 13-inch 7th Gen");
    add_names(@[ @"iPad15,7", @"iPad15,8" ], @"iPad 11th Gen");
    add_names(@[ @"iPad16,1", @"iPad16,2" ], @"iPad mini 7th Gen");
    add_names(@[ @"iPad16,3", @"iPad16,4" ], @"iPad Pro 11 inch 5th Gen");
    add_names(@[ @"iPad16,5", @"iPad16,6" ], @"iPad Pro 12.9 inch 7th Gen");

    overrides = [map copy];
  });

  NSString* display_name = overrides[machine];
  if ([display_name isKindOfClass:[NSString class]] && display_name.length > 0) {
    return display_name;
  }
  return machine;
}

static NSString* xe_device_display_name(void) {
  return xe_device_display_name_for_machine(xe_device_machine());
}

// ---------------------------------------------------------------------------
// Game art cache — downloads tile art from Element18592/360-Game-Art by
// title ID, caches to Library/Caches/game-art/{title_id_hex_lower}.png.
// ---------------------------------------------------------------------------
static NSString* xe_game_art_cache_dir(void) {
  static NSString* dir;
  if (!dir) {
    NSString* caches = NSSearchPathForDirectoriesInDomains(
        NSCachesDirectory, NSUserDomainMask, YES).firstObject;
    dir = [[caches stringByAppendingPathComponent:@"game-art"] retain];  // MRC
    [[NSFileManager defaultManager] createDirectoryAtPath:dir
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];
  }
  return dir;
}

static NSString* xe_game_art_hex(uint32_t title_id) {
  return [NSString stringWithFormat:@"%08x", title_id];
}

static NSMutableSet* xe_game_art_inflight_ids(void) {
  static NSMutableSet* inflight;
  if (!inflight) {
    inflight = [[NSMutableSet alloc] init];
  }
  return inflight;
}

static NSMutableDictionary* xe_game_art_retry_after(void) {
  static NSMutableDictionary* retry_after;
  if (!retry_after) {
    retry_after = [[NSMutableDictionary alloc] init];
  }
  return retry_after;
}

static bool xe_begin_game_art_fetch(NSString* hex) {
  if (!hex.length) {
    return false;
  }
  NSMutableSet* inflight = xe_game_art_inflight_ids();
  if ([inflight containsObject:hex]) {
    return false;
  }
  NSDate* retry_after = [xe_game_art_retry_after() objectForKey:hex];
  if (retry_after && [retry_after timeIntervalSinceNow] > 0) {
    return false;
  }
  [inflight addObject:hex];
  return true;
}

static void xe_complete_game_art_fetch(NSString* hex, bool success) {
  if (!hex.length) {
    return;
  }
  [xe_game_art_inflight_ids() removeObject:hex];
  NSMutableDictionary* retry_after = xe_game_art_retry_after();
  if (success) {
    [retry_after removeObjectForKey:hex];
  } else {
    // Throttle repeated failed downloads while still allowing periodic retries.
    [retry_after setObject:[NSDate dateWithTimeIntervalSinceNow:300.0]
                    forKey:hex];
  }
}

// Returns cached tile image synchronously, or nil if not yet cached.
static UIImage* xe_cached_game_art(uint32_t title_id) {
  if (!title_id) return nil;
  NSString* path = [xe_game_art_cache_dir()
      stringByAppendingPathComponent:
          [xe_game_art_hex(title_id) stringByAppendingString:@".jpg"]];
  return [UIImage imageWithContentsOfFile:path];
}

static NSString* xe_game_background_cache_dir(void) {
  static NSString* dir;
  if (!dir) {
    NSString* caches =
        NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject;
    dir = [[caches stringByAppendingPathComponent:@"game-background-art"] retain];
    [[NSFileManager defaultManager] createDirectoryAtPath:dir
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];
  }
  return dir;
}

static UIImage* xe_cached_game_background_art(uint32_t title_id) {
  if (!title_id) return nil;
  NSString* path = [xe_game_background_cache_dir()
      stringByAppendingPathComponent:[xe_game_art_hex(title_id) stringByAppendingString:@".jpg"]];
  return [UIImage imageWithContentsOfFile:path];
}

// Starts an async download of the tile art.  Calls `completion` on the main
// queue with the downloaded image (or nil on failure).
static void xe_fetch_game_art(uint32_t title_id,
                              void (^completion)(UIImage* _Nullable image)) {
  if (!title_id) {
    if (completion) completion(nil);
    return;
  }
  NSString* hex = xe_game_art_hex(title_id);
  if (!xe_begin_game_art_fetch(hex)) {
    return;
  }
  NSString* url_str = [NSString
      stringWithFormat:
          @"https://raw.githubusercontent.com/Element18592/360-Game-Art/main/Games/%@/cover.jpg",
          hex];
  NSURL* url = [NSURL URLWithString:url_str];
  if (!url) {
    dispatch_async(dispatch_get_main_queue(), ^{
      xe_complete_game_art_fetch(hex, false);
      if (completion) completion(nil);
    });
    return;
  }
  NSURLSessionDataTask* task = [[NSURLSession sharedSession]
      dataTaskWithURL:url
    completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
      UIImage* image = nil;
      if (!error && data.length > 0) {
        NSHTTPURLResponse* http = (NSHTTPURLResponse*)response;
        if ([http isKindOfClass:[NSHTTPURLResponse class]] && http.statusCode == 200) {
          image = [UIImage imageWithData:data];
          if (image) {
            NSString* path = [xe_game_art_cache_dir()
                stringByAppendingPathComponent:[hex stringByAppendingString:@".jpg"]];
            [data writeToFile:path atomically:YES];
          }
        }
      }
      const bool success = image != nil;
      dispatch_async(dispatch_get_main_queue(), ^{
        xe_complete_game_art_fetch(hex, success);
        if (completion) completion(image);
      });
    }];
  [task resume];
}

static void xe_fetch_game_background_art(uint32_t title_id,
                                         void (^completion)(UIImage* _Nullable image)) {
  if (!title_id) {
    if (completion) completion(nil);
    return;
  }
  NSString* hex_upper = [[NSString stringWithFormat:@"%08X", title_id] uppercaseString];
  NSString* url_str = [NSString stringWithFormat:@"https://raw.githubusercontent.com/xenia-manager/"
                                                 @"x360db/main/titles/%@/artwork/background.jpg",
                                                 hex_upper];
  NSURL* url = [NSURL URLWithString:url_str];
  if (!url) {
    if (completion) completion(nil);
    return;
  }
  NSURLSessionDataTask* task = [[NSURLSession sharedSession]
        dataTaskWithURL:url
      completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
        UIImage* image = nil;
        if (!error && data.length > 0) {
          NSHTTPURLResponse* http = (NSHTTPURLResponse*)response;
          if ([http isKindOfClass:[NSHTTPURLResponse class]] && http.statusCode == 200) {
            image = [UIImage imageWithData:data];
            if (image) {
              NSString* path = [xe_game_background_cache_dir()
                  stringByAppendingPathComponent:[xe_game_art_hex(title_id)
                                                     stringByAppendingString:@".jpg"]];
              [data writeToFile:path atomically:YES];
            }
          }
        }
        dispatch_async(dispatch_get_main_queue(), ^{
          if (completion) completion(image);
        });
      }];
  [task resume];
}

static NSString* xe_compat_cache_path(void) {
  NSString* caches = NSSearchPathForDirectoriesInDomains(
      NSCachesDirectory, NSUserDomainMask, YES)
                         .firstObject;
  return [caches stringByAppendingPathComponent:@"compat-data.json"];
}

static NSDictionary* xe_parse_compat_json(NSData* data) {
  if (!data || data.length == 0) {
    return nil;
  }
  NSError* error = nil;
  id parsed = [NSJSONSerialization JSONObjectWithData:data
                                              options:0
                                                error:&error];
  if (error || ![parsed isKindOfClass:[NSArray class]]) {
    return nil;
  }
  NSMutableDictionary* dict =
      [NSMutableDictionary dictionaryWithCapacity:[parsed count]];
  for (NSDictionary* game in parsed) {
    if (![game isKindOfClass:[NSDictionary class]]) {
      continue;
    }
    NSString* title_id = game[@"titleId"];
    if ([title_id isKindOfClass:[NSString class]] && title_id.length > 0) {
      [dict setObject:game forKey:[title_id uppercaseString]];
    }
  }
  return dict;
}

static NSDictionary* xe_load_cached_compat_data(void) {
  NSData* cached = [NSData dataWithContentsOfFile:xe_compat_cache_path()];
  if (cached.length == 0) {
    return nil;
  }
  return xe_parse_compat_json(cached);
}

static NSURLSession* xe_compat_url_session(void) {
  static NSURLSession* session = nil;
  static dispatch_once_t once_token;
  dispatch_once(&once_token, ^{
    NSURLSessionConfiguration* config =
        [NSURLSessionConfiguration defaultSessionConfiguration];
    if (@available(iOS 11.0, *)) {
      config.waitsForConnectivity = YES;
    }
    config.timeoutIntervalForRequest = 20.0;
    config.timeoutIntervalForResource = 45.0;
    session = [[NSURLSession sessionWithConfiguration:config] retain];
  });
  return session;
}

static void xe_fetch_compat_data(void (^completion)(NSDictionary* _Nullable data)) {
  NSData* cached = [NSData dataWithContentsOfFile:xe_compat_cache_path()];
  if (cached.length > 0) {
    NSDictionary* cached_result = xe_parse_compat_json(cached);
    if (cached_result) {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (completion) {
          completion(cached_result);
        }
      });
    }
  }

  NSURL* url =
      [NSURL URLWithString:@"https://xenios-compat-api.xenios.workers.dev/games"];
  NSURLSessionDataTask* task =
      [xe_compat_url_session()
          dataTaskWithURL:url
        completionHandler:^(NSData* data, NSURLResponse* response,
                            NSError* error) {
          if (error || data.length == 0) {
            return;
          }
          NSHTTPURLResponse* http = (NSHTTPURLResponse*)response;
          if (![http isKindOfClass:[NSHTTPURLResponse class]] ||
              http.statusCode != 200) {
            return;
          }
          if (cached.length > 0 && [cached isEqualToData:data]) {
            return;
          }
          NSDictionary* result = xe_parse_compat_json(data);
          if (!result) {
            return;
          }
          [data writeToFile:xe_compat_cache_path() atomically:YES];
          dispatch_async(dispatch_get_main_queue(), ^{
            if (completion) {
              completion(result);
            }
          });
        }];
  [task resume];
}

std::string DisplayNameFromXexMetadata(const std::filesystem::path& path,
                                       const std::optional<xe::vfs::XexMetadata>& metadata) {
  if (metadata.has_value() && metadata->title_id) {
    return "Title " + FormatTitleID(metadata->title_id);
  }
  return path.stem().string();
}

bool BuildDiscoveredGameFromPath(const std::filesystem::path& path, IOSDiscoveredGame* game_out) {
  if (!game_out) {
    return false;
  }

  IOSDiscoveredGame game;
  game.path = path;
  if (IsISOPath(path)) {
    auto metadata = xe::vfs::ExtractIsoMetadata(path);
    if (metadata.has_value()) {
      game.title_id = metadata->title_id;
    }
    game.title = NormalizeGameTitleForUI(DisplayNameFromXexMetadata(path, metadata));
    *game_out = std::move(game);
    return true;
  }

  if (IsDefaultXexPath(path)) {
    auto metadata = xe::vfs::ExtractXexMetadata(path);
    if (metadata.has_value()) {
      game.title_id = metadata->title_id;
    }
    game.title = NormalizeGameTitleForUI(DisplayNameFromXexMetadata(path, metadata));
    *game_out = std::move(game);
    return true;
  }

  if (!IsLikelyGodContainerFile(path)) {
    return false;
  }

  auto header = xe::vfs::XContentContainerDevice::ReadContainerHeader(path);
  if (!header || !header->content_header.is_magic_valid()) {
    return false;
  }

  if (header->content_metadata.data_file_count > 0 &&
      !HasContentSidecarDataDirectory(path)) {
    XELOGW("iOS: Skipping XContent package missing .data sidecar: {}", path);
    return false;
  }

  xe::XContentType content_type =
      static_cast<xe::XContentType>(header->content_metadata.content_type.get());
  if (content_type != xe::XContentType::kXbox360Title &&
      content_type != xe::XContentType::kInstalledGame) {
    return false;
  }

  game.title_id = header->content_metadata.execution_info.title_id;
  std::string display_name =
      xe::to_utf8(header->content_metadata.display_name(xe::XLanguage::kEnglish));
  if (display_name.empty()) {
    display_name = xe::to_utf8(header->content_metadata.title_name());
  }
  if (display_name.empty()) {
    std::string title_id = FormatTitleID(game.title_id);
    game.title = title_id.empty() ? path.stem().string() : "Title " + title_id;
  } else {
    game.title = display_name;
  }
  game.title = NormalizeGameTitleForUI(game.title);

  uint32_t thumb_size = header->content_metadata.title_thumbnail_size;
  if (thumb_size > 0 && thumb_size <= xe::vfs::XContentMetadata::kThumbLengthV1) {
    game.icon_data.assign(header->content_metadata.title_thumbnail,
                          header->content_metadata.title_thumbnail + thumb_size);
  }

  *game_out = std::move(game);
  return true;
}

std::vector<IOSConfigSection> BuildIOSConfigSections() {
  std::vector<IOSConfigSection> sections;

  IOSConfigSection graphics;
  graphics.title = "Graphics";
  graphics.footer = "These options affect image scaling and presentation.";
  AddBoolSetting(graphics.items, "present_letterbox", "Keep Aspect Ratio",
                 "Adds letterboxing to preserve image proportions.", true);
  AddChoiceSetting(graphics.items, IOSConfigControlType::kChoiceInt32, "present_safe_area_x",
                   "Safe Area (Horizontal)", "How much width is kept before cropping.", 100,
                   {{"90%", 90}, {"95%", 95}, {"100%", 100}});
  AddChoiceSetting(graphics.items, IOSConfigControlType::kChoiceInt32, "present_safe_area_y",
                   "Safe Area (Vertical)", "How much height is kept before cropping.", 100,
                   {{"90%", 90}, {"95%", 95}, {"100%", 100}});
  AddChoiceSetting(graphics.items, IOSConfigControlType::kChoiceInt32, "anisotropic_override",
                   "Anisotropic Filtering", "Texture filtering override level.", -1,
                   {{"Auto", -1}, {"Off", 0}, {"2x", 2}, {"4x", 4}, {"8x", 8}, {"16x", 16}});
  if (!graphics.items.empty()) {
    sections.push_back(std::move(graphics));
  }

  IOSConfigSection performance;
  performance.title = "Performance";
  performance.footer = "Some changes may require relaunching the current game.";
  AddChoiceSetting(performance.items, IOSConfigControlType::kChoiceUInt64, "framerate_limit",
                   "Frame Rate Limit", "Host presentation FPS cap. Guest timing is separate.", 0,
                   {{"Unlimited", 0}, {"30 FPS", 30}, {"60 FPS", 60}, {"120 FPS", 120}});
  AddBoolSetting(performance.items, "guest_display_refresh_cap", "Cap Guest Display Refresh",
                 "Runs guest vblank at PAL/NTSC rate instead of unlimited.", true);
  AddBoolSetting(performance.items, "async_shader_compilation", "Async Shader Compilation",
                 "Backend-dependent. Helps where supported; Metal support is currently limited.",
                 true);
  AddBoolSetting(performance.items, "half_pixel_offset", "Half-Pixel Offset",
                 "Compatibility adjustment for D3D9-style sampling.", true);
  AddBoolSetting(performance.items, "occlusion_query_enable", "Hardware Occlusion Queries",
                 "More accurate but can reduce performance.", false);
  if (!performance.items.empty()) {
    sections.push_back(std::move(performance));
  }

  IOSConfigSection compatibility;
  compatibility.title = "Compatibility";
  compatibility.footer = "Saved to xenia-edge.config.toml in the app Documents folder.";
  AddBoolSetting(compatibility.items, "gpu_allow_invalid_fetch_constants",
                 "Allow Invalid Fetch Constants",
                 "Unsafe workaround for games with bad fetch constants.", true);
  AddBoolSetting(compatibility.items, "gpu_allow_invalid_upload_range",
                 "Allow Invalid Upload Range", "Allows reads from pages marked no-access.", false);
  AddBoolSetting(compatibility.items, "ios_jit_brk_use_universal_0xf00d",
                 "Universal JIT Breakpoint",
                 "Use brk #0xF00D (x16 command dispatch) instead of legacy "
                 "brk #0x69.",
                 false);
  AddBoolSetting(compatibility.items, "ios_jit_non_txm_force_mprotect_flip",
                 "Force Mprotect Flip (Non-TXM)",
                 "Force RW/RX page flips on non-TXM iOS devices. "
                 "By default non-TXM still tries dual-map first. "
                 "Ignored when TXM is active.",
                 false);
  if (!compatibility.items.empty()) {
    sections.push_back(std::move(compatibility));
  }

  IOSConfigSection diagnostics;
  diagnostics.title = "Diagnostics";
  diagnostics.footer = "Logs are stored in Documents/xenia.log and can be viewed without Xcode.";
  AddChoiceSetting(diagnostics.items, IOSConfigControlType::kChoiceInt32, "log_level",
                   "Log Verbosity", "Lower values reduce log noise and file size.", 2,
                   {{"Errors Only", 0}, {"Warnings", 1}, {"Info", 2}, {"Debug", 3}});
  AddActionSetting(diagnostics.items, IOSConfigAction::kViewRecentLog, "View Live Log",
                   "Open a live-updating xenia.log viewer.");
  if (!diagnostics.items.empty()) {
    sections.push_back(std::move(diagnostics));
  }

  return sections;
}

bool ApplyIOSConfigSections(const std::vector<IOSConfigSection>& sections) {
  bool ok = true;
  for (const IOSConfigSection& section : sections) {
    for (const IOSConfigItem& item : section.items) {
      switch (item.control_type) {
        case IOSConfigControlType::kToggle:
          ok &= SetConfigVarBool(item.key, item.bool_value);
          break;
        case IOSConfigControlType::kChoiceInt32:
          ok &= SetConfigVarInt32(item.key, static_cast<int32_t>(item.choice_value));
          break;
        case IOSConfigControlType::kChoiceUInt64:
          ok &= SetConfigVarUInt64(item.key, static_cast<uint64_t>(item.choice_value));
          break;
        case IOSConfigControlType::kAction:
          break;
      }
    }
  }
  config::SaveConfig();
  return ok;
}

}  // namespace

@interface XeniaConfigViewController : UITableViewController
@end

typedef void (^IOSChoiceSelectionHandler)(int64_t value);
typedef void (^IOSProfileStatusHandler)(NSString* status_message);

@protocol XeniaGameContentHost <NSObject>
- (BOOL)installTitleUpdateAtPath:(NSString*)path
                          status:(NSString**)status_out
                  notTitleUpdate:(BOOL*)not_title_update_out;
- (void)refreshImportedGames;
@end

@interface XeniaChoiceListViewController : UITableViewController
- (instancetype)initWithTitle:(NSString*)title
                     subtitle:(NSString*)subtitle
                      choices:(const std::vector<IOSConfigChoice>&)choices
                selectedValue:(int64_t)selectedValue
                  onSelection:(IOSChoiceSelectionHandler)onSelection;
@end

@interface XeniaLogViewController : UIViewController
- (BOOL)handleControllerActions:(const xe::ui::apple::ControllerActionSet&)actions;
@end

@interface XeniaLandscapeNavigationController : UINavigationController
@end

@interface XeniaProfileViewController : UITableViewController
- (instancetype)initWithAppContext:(xe::ui::IOSWindowedAppContext*)app_context
                            onStatus:(IOSProfileStatusHandler)on_status;
@end

@interface XeniaGameContentViewController : UITableViewController <UIDocumentPickerDelegate>
- (instancetype)initWithTitleID:(uint32_t)title_id
                          title:(NSString*)title
                           host:(id<XeniaGameContentHost>)host;
@end

@interface XeniaGameCompatibilityViewController : UITableViewController
- (instancetype)initWithTitleID:(uint32_t)title_id
                          title:(NSString*)title
                     compatData:(NSDictionary*)compat_data;
- (void)setHeroArtwork:(UIImage*)image;
@end

@interface XeniaCompatReportViewController : UITableViewController <PHPickerViewControllerDelegate>
- (instancetype)initWithTitleID:(uint32_t)title_id title:(NSString*)title;
@end

@interface XeniaGameTileCell : UICollectionViewCell
@property(nonatomic, strong) UIView* cardView;
@property(nonatomic, strong) UIImageView* iconView;
@property(nonatomic, strong) UILabel* titleLabel;
@property(nonatomic, strong) XeniaPaddedLabel* compatPill;
@property(nonatomic, assign) BOOL controllerFocused;
@end

@implementation XeniaGameTileCell

- (instancetype)initWithFrame:(CGRect)frame {
  self = [super initWithFrame:frame];
  if (!self) {
    return nil;
  }

  self.contentView.backgroundColor = [UIColor clearColor];

  self.cardView = [[UIView alloc] init];
  self.cardView.translatesAutoresizingMaskIntoConstraints = NO;
  self.cardView.backgroundColor = [XeniaTheme bgSurface];
  self.cardView.layer.cornerRadius = XeniaRadiusLg;
  self.cardView.layer.borderWidth = 0.5;
  self.cardView.layer.borderColor = [XeniaTheme border].CGColor;
  self.cardView.clipsToBounds = YES;
  [self.contentView addSubview:self.cardView];

  self.iconView = [[UIImageView alloc] init];
  self.iconView.translatesAutoresizingMaskIntoConstraints = NO;
  self.iconView.contentMode = UIViewContentModeScaleAspectFill;
  self.iconView.clipsToBounds = YES;
  self.iconView.backgroundColor = [XeniaTheme bgSurface2];
  [self.cardView addSubview:self.iconView];

  self.titleLabel = [[UILabel alloc] init];
  self.titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
  self.titleLabel.textColor = [XeniaTheme textPrimary];
  self.titleLabel.textAlignment = NSTextAlignmentLeft;
  self.titleLabel.numberOfLines = 2;
  self.titleLabel.lineBreakMode = NSLineBreakByTruncatingTail;
  xe_apply_label_font(self.titleLabel, UIFontTextStyleSubheadline, 14.0,
                      UIFontWeightSemibold);
  [self.titleLabel setContentCompressionResistancePriority:UILayoutPriorityDefaultLow
                                                   forAxis:UILayoutConstraintAxisHorizontal];
  [self.titleLabel setContentCompressionResistancePriority:UILayoutPriorityRequired
                                                   forAxis:UILayoutConstraintAxisVertical];
  [self.cardView addSubview:self.titleLabel];

  self.compatPill = [[XeniaPaddedLabel alloc] init];
  self.compatPill.translatesAutoresizingMaskIntoConstraints = NO;
  self.compatPill.padding = UIEdgeInsetsMake(2, 8, 2, 8);
  self.compatPill.textAlignment = NSTextAlignmentCenter;
  self.compatPill.layer.cornerRadius = 8;
  self.compatPill.clipsToBounds = YES;
  xe_apply_label_font(self.compatPill, UIFontTextStyleCaption1, 12.0, UIFontWeightSemibold);
  self.compatPill.hidden = YES;
  [self.compatPill setContentHuggingPriority:UILayoutPriorityRequired
                                     forAxis:UILayoutConstraintAxisHorizontal];
  [self.compatPill setContentCompressionResistancePriority:UILayoutPriorityRequired
                                                   forAxis:UILayoutConstraintAxisHorizontal];
  [self.cardView addSubview:self.compatPill];

  [NSLayoutConstraint activateConstraints:@[
    [self.cardView.topAnchor constraintEqualToAnchor:self.contentView.topAnchor],
    [self.cardView.leadingAnchor constraintEqualToAnchor:self.contentView.leadingAnchor],
    [self.cardView.trailingAnchor constraintEqualToAnchor:self.contentView.trailingAnchor],
    [self.cardView.bottomAnchor constraintEqualToAnchor:self.contentView.bottomAnchor],
    [self.iconView.topAnchor constraintEqualToAnchor:self.cardView.topAnchor],
    [self.iconView.leadingAnchor constraintEqualToAnchor:self.cardView.leadingAnchor],
    [self.iconView.trailingAnchor constraintEqualToAnchor:self.cardView.trailingAnchor],
    [self.iconView.heightAnchor constraintEqualToAnchor:self.iconView.widthAnchor
                                             multiplier:300.0 / 219.0],
    [self.titleLabel.topAnchor constraintEqualToAnchor:self.iconView.bottomAnchor constant:8.0],
    [self.titleLabel.leadingAnchor constraintEqualToAnchor:self.cardView.leadingAnchor constant:12.0],
    [self.titleLabel.trailingAnchor constraintEqualToAnchor:self.cardView.trailingAnchor
                                                   constant:-12.0],
    [self.titleLabel.bottomAnchor constraintLessThanOrEqualToAnchor:self.compatPill.topAnchor
                                                           constant:-6.0],
    [self.titleLabel.bottomAnchor constraintLessThanOrEqualToAnchor:self.cardView.bottomAnchor
                                                           constant:-10.0],
    [self.compatPill.topAnchor constraintGreaterThanOrEqualToAnchor:self.titleLabel.bottomAnchor
                                                           constant:6.0],
    [self.compatPill.trailingAnchor constraintEqualToAnchor:self.cardView.trailingAnchor
                                                   constant:-10.0],
    [self.compatPill.bottomAnchor constraintEqualToAnchor:self.cardView.bottomAnchor
                                                 constant:-10.0],
    [self.compatPill.leadingAnchor constraintGreaterThanOrEqualToAnchor:self.cardView.leadingAnchor
                                                               constant:12.0],
  ]];

  return self;
}

- (void)prepareForReuse {
  [super prepareForReuse];
  self.iconView.image = nil;
  self.titleLabel.text = @"";
  self.compatPill.text = @"";
  self.compatPill.hidden = YES;
  self.cardView.layer.borderColor = [XeniaTheme border].CGColor;
  self.controllerFocused = NO;
}

- (void)setControllerFocused:(BOOL)controllerFocused {
  if (_controllerFocused == controllerFocused) {
    return;
  }
  _controllerFocused = controllerFocused;
  [self updateControllerFocusAppearance];
}

- (void)updateControllerFocusAppearance {
  if (self.controllerFocused) {
    self.cardView.layer.borderColor = [XeniaTheme accent].CGColor;
    self.titleLabel.textColor = [XeniaTheme textPrimary];
    self.transform = CGAffineTransformMakeScale(1.02, 1.02);
  } else {
    self.cardView.layer.borderColor = [XeniaTheme border].CGColor;
    self.titleLabel.textColor = [XeniaTheme textPrimary];
    self.transform = CGAffineTransformIdentity;
  }
}

@end

@implementation XeniaChoiceListViewController {
  std::vector<IOSConfigChoice> choices_;
  int64_t selected_value_;
  NSString* subtitle_;
  IOSChoiceSelectionHandler on_selection_;
}

- (instancetype)initWithTitle:(NSString*)title
                     subtitle:(NSString*)subtitle
                      choices:(const std::vector<IOSConfigChoice>&)choices
                selectedValue:(int64_t)selectedValue
                  onSelection:(IOSChoiceSelectionHandler)onSelection {
  self = [super initWithStyle:UITableViewStyleInsetGrouped];
  if (self) {
    self.title = title;
    subtitle_ = [subtitle copy];
    choices_ = choices;
    selected_value_ = selectedValue;
    on_selection_ = [onSelection copy];
  }
  return self;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.tableView.backgroundColor = [UIColor systemBackgroundColor];
  self.tableView.separatorInset = UIEdgeInsetsMake(0, 16, 0, 16);

  if (subtitle_.length > 0) {
    UILabel* header_label = [[UILabel alloc] initWithFrame:CGRectZero];
    header_label.text = subtitle_;
    header_label.textColor = [XeniaTheme textSecondary];
    header_label.font = [UIFont systemFontOfSize:13];
    header_label.numberOfLines = 0;
    header_label.textAlignment = NSTextAlignmentLeft;
    header_label.translatesAutoresizingMaskIntoConstraints = NO;

    UIView* header = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 1, 56)];
    [header addSubview:header_label];
    [NSLayoutConstraint activateConstraints:@[
      [header_label.leadingAnchor constraintEqualToAnchor:header.leadingAnchor constant:18],
      [header_label.trailingAnchor constraintEqualToAnchor:header.trailingAnchor constant:-18],
      [header_label.topAnchor constraintEqualToAnchor:header.topAnchor constant:8],
      [header_label.bottomAnchor constraintEqualToAnchor:header.bottomAnchor constant:-8],
    ]];
    self.tableView.tableHeaderView = header;
  }
}

- (NSInteger)tableView:(UITableView* __unused)tableView
    numberOfRowsInSection:(NSInteger)__unused section {
  return static_cast<NSInteger>(choices_.size());
}

- (UITableViewCell*)tableView:(UITableView*)tableView
        cellForRowAtIndexPath:(NSIndexPath*)indexPath {
  static NSString* const kChoiceCellIdentifier = @"XeniaChoiceCell";
  UITableViewCell* cell = [tableView dequeueReusableCellWithIdentifier:kChoiceCellIdentifier];
  if (!cell) {
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                  reuseIdentifier:kChoiceCellIdentifier];
  }
  if (indexPath.row < 0 || indexPath.row >= static_cast<NSInteger>(choices_.size())) {
    cell.textLabel.text = @"";
    cell.accessoryType = UITableViewCellAccessoryNone;
    return cell;
  }
  const IOSConfigChoice& choice = choices_[indexPath.row];
  cell.textLabel.text = ToNSString(choice.title);
  cell.accessoryType = (choice.value == selected_value_) ? UITableViewCellAccessoryCheckmark
                                                         : UITableViewCellAccessoryNone;
  return cell;
}

- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)indexPath {
  if (indexPath.row < 0 || indexPath.row >= static_cast<NSInteger>(choices_.size())) {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    return;
  }
  const IOSConfigChoice& choice = choices_[indexPath.row];
  selected_value_ = choice.value;
  if (on_selection_) {
    on_selection_(selected_value_);
  }
  [self.navigationController popViewControllerAnimated:YES];
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
  return UIInterfaceOrientationMaskAllButUpsideDown;
}

- (UIInterfaceOrientation)preferredInterfaceOrientationForPresentation {
  return UIInterfaceOrientationPortrait;
}

@end

@implementation XeniaProfileViewController {
  xe::ui::IOSWindowedAppContext* app_context_;
  IOSProfileStatusHandler on_status_;
  std::vector<xe::ui::IOSProfileSummary> profiles_;
}

- (instancetype)initWithAppContext:(xe::ui::IOSWindowedAppContext*)app_context
                          onStatus:(IOSProfileStatusHandler)on_status {
  self = [super initWithStyle:UITableViewStyleInsetGrouped];
  if (self) {
    app_context_ = app_context;
    on_status_ = [on_status copy];
    self.title = @"Profiles";
  }
  return self;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.tableView.backgroundColor = [UIColor systemBackgroundColor];
  self.tableView.separatorInset = UIEdgeInsetsMake(0, 16, 0, 16);
  self.navigationItem.leftBarButtonItem =
      [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                                                    target:self
                                                    action:@selector(doneTapped:)];
}

- (void)viewWillAppear:(BOOL)animated {
  [super viewWillAppear:animated];
  [self reloadProfiles];
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
  return UIInterfaceOrientationMaskAllButUpsideDown;
}

- (UIInterfaceOrientation)preferredInterfaceOrientationForPresentation {
  return UIInterfaceOrientationPortrait;
}

- (void)doneTapped:(id)sender {
  [self dismissViewControllerAnimated:YES completion:nil];
}

- (void)reloadProfiles {
  profiles_.clear();
  if (app_context_) {
    profiles_ = app_context_->ListProfiles();
  }
  [self.tableView reloadData];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView* __unused)tableView {
  return 2;
}

- (NSInteger)tableView:(UITableView* __unused)tableView
 numberOfRowsInSection:(NSInteger)section {
  if (section == 0) {
    return 1;
  }
  return static_cast<NSInteger>(profiles_.size());
}

- (NSString*)tableView:(UITableView* __unused)tableView
titleForHeaderInSection:(NSInteger)section {
  if (section == 0) {
    return @"Actions";
  }
  return @"Local Profiles";
}

- (NSString*)tableView:(UITableView* __unused)tableView
titleForFooterInSection:(NSInteger)section {
  if (section == 0) {
    return @"Create and sign in profiles used by Xbox Live emulation.";
  }
  if (profiles_.empty()) {
    return @"No profiles created yet.";
  }
  return nil;
}

- (UITableViewCell*)tableView:(UITableView*)tableView
        cellForRowAtIndexPath:(NSIndexPath*)indexPath {
  static NSString* const kCellIdentifier = @"XeniaProfileCell";
  UITableViewCell* cell = [tableView dequeueReusableCellWithIdentifier:kCellIdentifier];
  if (!cell) {
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                  reuseIdentifier:kCellIdentifier];
  }

  cell.textLabel.numberOfLines = 1;
  cell.detailTextLabel.numberOfLines = 1;
  cell.accessoryType = UITableViewCellAccessoryNone;
  cell.accessoryView = nil;
  cell.selectionStyle = UITableViewCellSelectionStyleDefault;

  if (indexPath.section == 0) {
    cell.textLabel.text = @"Create Profile";
    cell.textLabel.textColor = self.view.tintColor;
    cell.detailTextLabel.text = @"Create a new profile and sign in.";
    cell.detailTextLabel.textColor = [XeniaTheme textSecondary];
    cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    return cell;
  }

  if (indexPath.row < 0 || indexPath.row >= static_cast<NSInteger>(profiles_.size())) {
    cell.textLabel.text = @"";
    cell.detailTextLabel.text = @"";
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    return cell;
  }

  const auto& profile = profiles_[indexPath.row];
  NSString* gamertag = ToNSString(profile.gamertag);
  cell.textLabel.text = gamertag;
  cell.textLabel.textColor = [XeniaTheme textPrimary];
  if (profile.signed_in) {
    cell.detailTextLabel.text =
        [NSString stringWithFormat:@"Signed in on slot %u", profile.signed_in_slot];
    cell.accessoryType = UITableViewCellAccessoryCheckmark;
  } else {
    cell.detailTextLabel.text = @"Not signed in";
  }
  cell.detailTextLabel.textColor = [XeniaTheme textSecondary];
  return cell;
}

- (void)presentCreateProfileAlert {
  if (!app_context_) {
    return;
  }

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
  [create_alert addAction:[UIAlertAction
                              actionWithTitle:@"Create"
                                        style:UIAlertActionStyleDefault
                                      handler:^(__unused UIAlertAction* action) {
                                        UITextField* text_field =
                                            create_alert.textFields.firstObject;
                                        NSString* raw_text = text_field.text ?: @"";
                                        NSString* trimmed = [raw_text
                                            stringByTrimmingCharactersInSet:
                                                [NSCharacterSet
                                                    whitespaceAndNewlineCharacterSet]];
                                        if (trimmed.length == 0 || !app_context_) {
                                          return;
                                        }
                                        NSString* gamertag = [[trimmed copy] autorelease];
                                        create_alert = nil;
                                        uint64_t xuid = app_context_->CreateProfile(
                                            std::string([gamertag UTF8String]));
                                        if (!xuid || !app_context_->SignInProfile(xuid)) {
                                          if (on_status_) {
                                            on_status_(@"Failed to create profile.");
                                          }
                                          return;
                                        }
                                        if (on_status_) {
                                          on_status_([NSString
                                              stringWithFormat:@"Signed in as %@.", gamertag]);
                                        }
                                        [self reloadProfiles];
                                      }]];
  [self presentViewController:create_alert animated:YES completion:nil];
}

- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)indexPath {
  [tableView deselectRowAtIndexPath:indexPath animated:YES];
  if (!app_context_) {
    return;
  }

  if (indexPath.section == 0) {
    [self presentCreateProfileAlert];
    return;
  }

  if (indexPath.row < 0 || indexPath.row >= static_cast<NSInteger>(profiles_.size())) {
    return;
  }

  const auto& profile = profiles_[indexPath.row];
  if (app_context_->SignInProfile(profile.xuid)) {
    if (on_status_) {
      on_status_([NSString stringWithFormat:@"Signed in as %@.", ToNSString(profile.gamertag)]);
    }
    [self reloadProfiles];
  } else if (on_status_) {
    on_status_(@"Failed to sign in profile.");
  }
}

@end

@implementation XeniaGameContentViewController {
  uint32_t title_id_;
  NSString* game_title_;
  id<XeniaGameContentHost> host_;  // not retained; presenter owns this sheet
  std::vector<IOSInstalledContentEntry> installed_content_;
}

- (instancetype)initWithTitleID:(uint32_t)title_id
                          title:(NSString*)title
                           host:(id<XeniaGameContentHost>)host {
  self = [super initWithStyle:UITableViewStyleInsetGrouped];
  if (self) {
    title_id_ = title_id;
    game_title_ = [title copy];
    host_ = host;
    self.title = @"Manage Content";
  }
  return self;
}

- (void)dealloc {
  [game_title_ release];
  [super dealloc];
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.tableView.backgroundColor = [UIColor systemBackgroundColor];
  self.tableView.separatorInset = UIEdgeInsetsMake(0, 16, 0, 16);
  self.navigationItem.leftBarButtonItem =
      [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                                                    target:self
                                                    action:@selector(doneTapped:)];
}

- (void)viewWillAppear:(BOOL)animated {
  [super viewWillAppear:animated];
  [self reloadInstalledContent];
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
  return UIInterfaceOrientationMaskAllButUpsideDown;
}

- (UIInterfaceOrientation)preferredInterfaceOrientationForPresentation {
  return UIInterfaceOrientationPortrait;
}

- (void)doneTapped:(id)__unused sender {
  if (self.navigationController.presentingViewController &&
      self.navigationController.viewControllers.firstObject == self) {
    [self.navigationController dismissViewControllerAnimated:YES completion:nil];
    return;
  }
  [self.navigationController popViewControllerAnimated:YES];
}

- (void)reloadInstalledContent {
  installed_content_ = xe_list_installed_content(title_id_);
  [self.tableView reloadData];
}

- (void)refreshLauncherContentState {
  if (host_) {
    [host_ refreshImportedGames];
  }
  [self reloadInstalledContent];
}

- (void)presentAddContentPicker {
  UIDocumentPickerViewController* picker =
      [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeData ]];
  picker.delegate = self;
  picker.allowsMultipleSelection = NO;
  picker.shouldShowFileExtensions = YES;
  [self presentViewController:picker animated:YES completion:nil];
  [picker release];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView* __unused)tableView {
  return 2;
}

- (NSInteger)tableView:(UITableView* __unused)tableView numberOfRowsInSection:(NSInteger)section {
  if (section == 0) {
    return 1;
  }
  return installed_content_.empty() ? 1 : static_cast<NSInteger>(installed_content_.size());
}

- (NSString*)tableView:(UITableView* __unused)tableView titleForHeaderInSection:(NSInteger)section {
  if (section == 0) {
    return @"Actions";
  }
  return @"Installed Content";
}

- (NSString*)tableView:(UITableView* __unused)tableView titleForFooterInSection:(NSInteger)section {
  if (section == 0) {
    if (game_title_.length > 0) {
      return
          [NSString stringWithFormat:@"Install title updates or DLC packages for %@.", game_title_];
    }
    return @"Install title updates or DLC packages for this game.";
  }
  if (installed_content_.empty()) {
    return @"No title updates or DLC are installed for this title.";
  }
  return @"Swipe left on an installed entry to delete it.";
}

- (UITableViewCell*)tableView:(UITableView*)tableView
        cellForRowAtIndexPath:(NSIndexPath*)indexPath {
  if (indexPath.section == 0) {
    static NSString* const kActionCellIdentifier = @"XeniaGameContentActionCell";
    UITableViewCell* cell = [tableView dequeueReusableCellWithIdentifier:kActionCellIdentifier];
    if (!cell) {
      cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                     reuseIdentifier:kActionCellIdentifier] autorelease];
    }
    cell.textLabel.text = @"Add Content";
    cell.textLabel.textColor = self.view.tintColor;
    cell.detailTextLabel.text = @"Import a title update or DLC package for this game.";
    cell.detailTextLabel.textColor = [XeniaTheme textSecondary];
    cell.imageView.image = [UIImage systemImageNamed:@"plus.circle.fill"];
    cell.imageView.tintColor = self.view.tintColor;
    cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    cell.selectionStyle = UITableViewCellSelectionStyleDefault;
    return cell;
  }

  if (installed_content_.empty()) {
    static NSString* const kEmptyCellIdentifier = @"XeniaGameContentEmptyCell";
    UITableViewCell* cell = [tableView dequeueReusableCellWithIdentifier:kEmptyCellIdentifier];
    if (!cell) {
      cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                     reuseIdentifier:kEmptyCellIdentifier] autorelease];
    }
    cell.textLabel.text = @"No content installed";
    cell.detailTextLabel.text = @"Add a title update or DLC package from Files.";
    cell.textLabel.textColor = [XeniaTheme textMuted];
    cell.detailTextLabel.textColor = [XeniaTheme textSecondary];
    cell.accessoryType = UITableViewCellAccessoryNone;
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    return cell;
  }

  static NSString* const kContentCellIdentifier = @"XeniaGameContentCell";
  UITableViewCell* cell = [tableView dequeueReusableCellWithIdentifier:kContentCellIdentifier];
  if (!cell) {
    cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                   reuseIdentifier:kContentCellIdentifier] autorelease];
  }

  const IOSInstalledContentEntry& entry = installed_content_[static_cast<size_t>(indexPath.row)];
  cell.textLabel.text = ToNSString(entry.name);
  cell.textLabel.textColor = [XeniaTheme textPrimary];
  cell.detailTextLabel.text = xe_installed_content_kind_label(entry.kind);
  cell.detailTextLabel.textColor = [XeniaTheme textSecondary];
  cell.accessoryType = UITableViewCellAccessoryNone;
  cell.selectionStyle = UITableViewCellSelectionStyleNone;
  return cell;
}

- (BOOL)tableView:(UITableView* __unused)tableView canEditRowAtIndexPath:(NSIndexPath*)indexPath {
  return indexPath.section == 1 && !installed_content_.empty();
}

- (void)tableView:(UITableView* __unused)tableView
    commitEditingStyle:(UITableViewCellEditingStyle)editingStyle
     forRowAtIndexPath:(NSIndexPath*)indexPath {
  if (editingStyle != UITableViewCellEditingStyleDelete || indexPath.section != 1 ||
      installed_content_.empty()) {
    return;
  }

  const IOSInstalledContentEntry& entry = installed_content_[static_cast<size_t>(indexPath.row)];
  const std::filesystem::path entry_path = entry.path;
  NSString* display_name = ToNSString(entry.name);
  UIAlertController* confirm = [UIAlertController
      alertControllerWithTitle:@"Delete Content"
                       message:[NSString stringWithFormat:@"Delete \"%@\"? This cannot be undone.",
                                                          display_name]
                preferredStyle:UIAlertControllerStyleAlert];
  [confirm addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                              style:UIAlertActionStyleCancel
                                            handler:nil]];
  [confirm
      addAction:[UIAlertAction
                    actionWithTitle:@"Delete"
                              style:UIAlertActionStyleDestructive
                            handler:^(__unused UIAlertAction* action) {
                              std::error_code ec;
                              std::filesystem::remove_all(entry_path, ec);
                              if (ec) {
                                xe_present_ok_alert(
                                    self, @"Delete Failed",
                                    [NSString stringWithFormat:@"Failed deleting %@: %s",
                                                               display_name, ec.message().c_str()]);
                                return;
                              }
                              [self refreshLauncherContentState];
                            }]];
  [self presentViewController:confirm animated:YES completion:nil];
}

- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)indexPath {
  [tableView deselectRowAtIndexPath:indexPath animated:YES];
  if (indexPath.section == 0) {
    [self presentAddContentPicker];
  }
}

- (void)documentPicker:(UIDocumentPickerViewController* __unused)controller
    didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
  if (urls.count == 0) {
    return;
  }

  NSURL* url = urls[0];
  BOOL access_granted = [url startAccessingSecurityScopedResource];
  IOSSelectedContentPackage package_info;
  NSString* validation_error = nil;
  if (!xe_read_selected_content_package(std::filesystem::path([url.path UTF8String]), &package_info,
                                        &validation_error)) {
    if (access_granted) {
      [url stopAccessingSecurityScopedResource];
    }
    xe_present_ok_alert(self, @"Invalid Package",
                        validation_error ?: @"Could not read the selected content package.");
    return;
  }

  if (package_info.title_id != title_id_) {
    if (access_granted) {
      [url stopAccessingSecurityScopedResource];
    }
    xe_present_ok_alert(
        self, @"Wrong Game",
        [NSString stringWithFormat:@"This package is for title %08X, but the current game is %08X.",
                                   package_info.title_id, title_id_]);
    return;
  }

  BOOL install_success = NO;
  NSString* result_title = @"Unsupported Content";
  NSString* result_message = nil;
  switch (package_info.content_type) {
    case xe::XContentType::kInstaller: {
      NSString* status_message = nil;
      install_success = host_ && [host_ installTitleUpdateAtPath:url.path
                                                          status:&status_message
                                                  notTitleUpdate:nullptr];
      result_title = install_success ? @"Installed" : @"Install Failed";
      result_message = install_success ? (status_message ?: @"Title update installed successfully.")
                                       : (status_message ?: @"Title update installation failed.");
    } break;
    case xe::XContentType::kMarketplaceContent: {
      std::string error_message;
      install_success = xe_copy_content_package_into_root(
          package_info, xe_dlc_content_root(title_id_), &error_message);
      result_title = install_success ? @"Installed" : @"Install Failed";
      result_message =
          install_success
              ? @"DLC installed successfully."
              : ToNSString(error_message.empty() ? "DLC installation failed." : error_message);
    } break;
    default:
      result_message =
          [NSString stringWithFormat:@"Content type 0x%08X is not a title update or DLC.",
                                     static_cast<uint32_t>(package_info.content_type)];
      break;
  }

  if (access_granted) {
    [url stopAccessingSecurityScopedResource];
  }

  if (install_success) {
    [self refreshLauncherContentState];
  }
  xe_present_ok_alert(self, result_title, result_message);
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController* __unused)controller {
  XELOGI("iOS: Manage Content picker cancelled");
}

@end

static constexpr NSInteger kXeniaDiscussionPreviewCount = 3;

@implementation XeniaGameCompatibilityViewController {
  uint32_t title_id_;
  NSString* game_title_;
  NSDictionary* compat_info_;
  UIImage* hero_artwork_;
  UIImage* hero_background_artwork_;
  UIView* hero_header_view_;
  UIView* hero_background_view_;
  UIView* hero_header_card_view_;
  UIImageView* hero_header_backdrop_view_;
  UIVisualEffectView* hero_header_blur_view_;
  CAGradientLayer* hero_header_scrim_layer_;
  UILabel* hero_title_label_;
  UILabel* hero_tid_label_;
  UIStackView* hero_pills_stack_;
  XeniaPaddedLabel* hero_status_pill_;
  XeniaPaddedLabel* hero_perf_pill_;
  UILabel* hero_updated_label_;
  NSMutableArray<NSDictionary*>* discussion_reports_;
  NSMutableSet<NSNumber*>* discussion_expanded_report_indexes_;
  NSString* discussion_issue_url_;
  NSInteger discussion_issue_number_;
  BOOL discussion_loading_;
  BOOL discussion_show_all_;
  BOOL hero_scroll_layout_initialized_;
}

- (instancetype)initWithTitleID:(uint32_t)title_id
                          title:(NSString*)title
                     compatData:(NSDictionary*)compat_data {
  self = [super initWithStyle:UITableViewStylePlain];
  if (self) {
    title_id_ = title_id;
    game_title_ = [title copy];
    compat_info_ = [compat_data retain];
    discussion_reports_ = [[NSMutableArray alloc] init];
    discussion_expanded_report_indexes_ = [[NSMutableSet alloc] init];
    discussion_loading_ = YES;
    discussion_show_all_ = NO;
    discussion_issue_number_ = 0;
    self.title = @"Compatibility";
  }
  return self;
}

- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [hero_background_view_ removeFromSuperview];
  [game_title_ release];
  [compat_info_ release];
  [hero_artwork_ release];
  [hero_background_artwork_ release];
  [hero_header_view_ release];
  [hero_background_view_ release];
  [hero_header_card_view_ release];
  [hero_header_backdrop_view_ release];
  [hero_header_blur_view_ release];
  [hero_header_scrim_layer_ release];
  [hero_title_label_ release];
  [hero_tid_label_ release];
  [hero_pills_stack_ release];
  [hero_status_pill_ release];
  [hero_perf_pill_ release];
  [hero_updated_label_ release];
  [discussion_reports_ release];
  [discussion_expanded_report_indexes_ release];
  [discussion_issue_url_ release];
  [super dealloc];
}

- (void)setHeroArtwork:(UIImage*)image {
  [hero_artwork_ release];
  hero_artwork_ = [image retain];
}

- (void)setHeroBackgroundArtwork:(UIImage*)image {
  [hero_background_artwork_ release];
  hero_background_artwork_ = [image retain];
}

- (NSDictionary*)bestResultSource {
  if (compat_info_) {
    return compat_info_;
  }
  return [self latestDiscussionReport];
}

- (void)updateHeroHeaderArtwork {
  if (!hero_header_backdrop_view_) {
    return;
  }
  UIImage* display = hero_background_artwork_ ?: hero_artwork_;
  hero_header_backdrop_view_.image = display;
  hero_header_backdrop_view_.hidden = (display == nil);
  hero_header_backdrop_view_.layer.contentsRect = hero_background_artwork_
      ? xe_compat_background_contents_rect(display, hero_header_card_view_.bounds.size)
      : CGRectMake(0.0, 0.18, 1.0, 0.82);
  hero_header_blur_view_.alpha = hero_background_artwork_ ? 0.22 : 0.30;
  hero_header_card_view_.backgroundColor =
      display ? [XeniaTheme bgSurface] : [XeniaTheme bgSurface2];
  hero_header_scrim_layer_.colors = @[
    (id)[UIColor colorWithWhite:0.0 alpha:0.10].CGColor,
    (id)[UIColor colorWithWhite:0.0 alpha:0.40].CGColor,
    (id)[UIColor colorWithWhite:0.0 alpha:0.82].CGColor,
  ];
  hero_header_scrim_layer_.locations = @[ @0.0, @0.56, @1.0 ];
}

- (void)updateHeroHeaderContent {
  if (!hero_header_view_) {
    return;
  }

  hero_title_label_.text = game_title_.length > 0 ? game_title_ : @"Unknown Title";
  hero_tid_label_.text = title_id_ ? [NSString stringWithFormat:@"Title ID: %08X", title_id_]
                                   : @"No title ID available";

  NSDictionary* summary_source = [self bestResultSource];
  NSDictionary* fallback_source = [self latestDiscussionReport];
  NSString* status =
      [summary_source[@"status"] isKindOfClass:[NSString class]] ? summary_source[@"status"] : nil;
  if (status.length == 0) {
    status = [fallback_source[@"status"] isKindOfClass:[NSString class]] ? fallback_source[@"status"]
                                                                         : nil;
  }
  if (status.length > 0) {
    UIColor* status_color = xe_compat_status_color(status);
    hero_status_pill_.text = xe_compat_status_label(status);
    hero_status_pill_.textColor = status_color;
    hero_status_pill_.backgroundColor = [status_color colorWithAlphaComponent:0.1];
    hero_status_pill_.hidden = NO;
  } else {
    hero_status_pill_.hidden = YES;
  }

  NSString* perf =
      [summary_source[@"perf"] isKindOfClass:[NSString class]] ? summary_source[@"perf"] : nil;
  if (perf.length == 0) {
    perf = [fallback_source[@"perf"] isKindOfClass:[NSString class]] ? fallback_source[@"perf"]
                                                                     : nil;
  }
  if (perf.length > 0) {
    UIColor* perf_color = xe_compat_perf_color(perf);
    hero_perf_pill_.text = xe_compat_perf_label(perf);
    hero_perf_pill_.textColor = perf_color;
    hero_perf_pill_.backgroundColor = [perf_color colorWithAlphaComponent:0.1];
    hero_perf_pill_.hidden = NO;
  } else {
    hero_perf_pill_.hidden = YES;
  }
  hero_pills_stack_.hidden = hero_status_pill_.hidden && hero_perf_pill_.hidden;

  NSString* updated_at =
      [compat_info_[@"updatedAt"] isKindOfClass:[NSString class]]
          ? compat_info_[@"updatedAt"]
          : ([summary_source[@"date"] isKindOfClass:[NSString class]] ? summary_source[@"date"]
                                                                      : nil);
  if (updated_at.length == 0) {
    updated_at =
        [fallback_source[@"date"] isKindOfClass:[NSString class]] ? fallback_source[@"date"] : nil;
  }
  if (updated_at.length > 0) {
    hero_updated_label_.text =
        [NSString stringWithFormat:@"Last updated: %@", xe_format_iso_date(updated_at)];
    hero_updated_label_.hidden = NO;
  } else {
    hero_updated_label_.hidden = YES;
  }

  [self updateHeroHeaderArtwork];
}

- (void)layoutHeroHeaderIfNeeded {
  if (!hero_header_view_) {
    return;
  }
  CGFloat width = CGRectGetWidth(self.tableView.bounds);
  if (width <= 0.0) {
    width = CGRectGetWidth(self.view.bounds);
  }
  if (width <= 0.0) {
    return;
  }

  CGRect frame = hero_header_view_.frame;
  CGFloat height = xe_compat_hero_height_for_width(width);
  if (!hero_background_view_) {
    hero_background_view_ = [[UIView alloc] initWithFrame:CGRectZero];
    hero_background_view_.backgroundColor = [UIColor clearColor];
    hero_background_view_.clipsToBounds = NO;
    [hero_background_view_ addSubview:hero_header_view_];
  }
  UIView* host_view = self.navigationController.view ?: self.view.superview ?: self.view;
  if (host_view && hero_background_view_.superview != host_view) {
    [hero_background_view_ removeFromSuperview];
    [host_view addSubview:hero_background_view_];
  }
  CGRect table_frame =
      host_view ? [self.view convertRect:self.view.bounds toView:host_view] : self.view.bounds;
  CGFloat hero_top = CGRectGetMinY(table_frame);
  hero_background_view_.frame =
      CGRectMake(CGRectGetMinX(table_frame), hero_top, width, height);
  if (fabs(frame.size.width - width) > 0.5 || fabs(frame.size.height - height) > 0.5) {
    frame.size.width = width;
    frame.size.height = height;
    hero_header_view_.frame = frame;
  }
  CGFloat desired_top_inset =
      CGRectGetMaxY(hero_background_view_.frame) - CGRectGetMinY(table_frame) + 12.0;
  CGFloat relative_offset = self.tableView.contentOffset.y + self.tableView.contentInset.top;
  if (fabs(self.tableView.contentInset.top - desired_top_inset) > 0.5) {
    UIEdgeInsets content_inset = self.tableView.contentInset;
    content_inset.top = desired_top_inset;
    self.tableView.contentInset = content_inset;
    if (@available(iOS 13.0, *)) {
      UIEdgeInsets vertical_insets = self.tableView.verticalScrollIndicatorInsets;
      vertical_insets.top = desired_top_inset;
      self.tableView.verticalScrollIndicatorInsets = vertical_insets;
    } else {
      UIEdgeInsets indicator_insets = content_inset;
      indicator_insets.top = desired_top_inset;
      self.tableView.scrollIndicatorInsets = indicator_insets;
    }
    self.tableView.contentOffset =
        CGPointMake(self.tableView.contentOffset.x, relative_offset - desired_top_inset);
  }
  if (!hero_scroll_layout_initialized_) {
    self.tableView.contentOffset = CGPointMake(self.tableView.contentOffset.x, -desired_top_inset);
    hero_scroll_layout_initialized_ = YES;
  }
  [hero_header_view_ setNeedsLayout];
  [hero_header_view_ layoutIfNeeded];
  hero_header_scrim_layer_.frame = hero_header_card_view_.bounds;
  [self updateHeroHeaderArtwork];
}

- (void)buildHeroHeaderIfNeeded {
  if (hero_header_view_) {
    [self updateHeroHeaderContent];
    [self layoutHeroHeaderIfNeeded];
    return;
  }

  CGFloat width = CGRectGetWidth(self.tableView.bounds);
  if (width <= 0.0) {
    width = CGRectGetWidth(self.view.bounds);
  }
  if (width <= 0.0) {
    width = UIScreen.mainScreen.bounds.size.width;
  }

  CGFloat height = xe_compat_hero_height_for_width(width);
  hero_header_view_ = [[UIView alloc] initWithFrame:CGRectMake(0.0, 0.0, width, height)];
  hero_header_view_.backgroundColor = [UIColor clearColor];
  hero_background_view_ = [[UIView alloc] initWithFrame:hero_header_view_.frame];
  hero_background_view_.backgroundColor = [UIColor clearColor];
  hero_background_view_.clipsToBounds = NO;
  [hero_background_view_ addSubview:hero_header_view_];

  hero_header_card_view_ = [[UIView alloc] init];
  hero_header_card_view_.translatesAutoresizingMaskIntoConstraints = NO;
  hero_header_card_view_.backgroundColor = [XeniaTheme bgSurface];
  hero_header_card_view_.layer.cornerRadius = 28.0;
  if (@available(iOS 11.0, *)) {
    hero_header_card_view_.layer.maskedCorners =
        kCALayerMinXMinYCorner | kCALayerMaxXMinYCorner;
  }
  hero_header_card_view_.layer.borderWidth = 0.5;
  hero_header_card_view_.layer.borderColor = [XeniaTheme border].CGColor;
  hero_header_card_view_.clipsToBounds = YES;
  [hero_header_view_ addSubview:hero_header_card_view_];

  hero_header_backdrop_view_ = [[UIImageView alloc] init];
  hero_header_backdrop_view_.translatesAutoresizingMaskIntoConstraints = NO;
  hero_header_backdrop_view_.contentMode = UIViewContentModeScaleAspectFill;
  hero_header_backdrop_view_.clipsToBounds = YES;
  [hero_header_card_view_ addSubview:hero_header_backdrop_view_];

  hero_header_blur_view_ = [[UIVisualEffectView alloc]
      initWithEffect:[UIBlurEffect effectWithStyle:UIBlurEffectStyleSystemUltraThinMaterialDark]];
  hero_header_blur_view_.translatesAutoresizingMaskIntoConstraints = NO;
  [hero_header_card_view_ addSubview:hero_header_blur_view_];

  hero_header_scrim_layer_ = [[CAGradientLayer layer] retain];
  [hero_header_card_view_.layer addSublayer:hero_header_scrim_layer_];

  UIView* handle_view = [[[UIView alloc] init] autorelease];
  handle_view.translatesAutoresizingMaskIntoConstraints = NO;
  handle_view.backgroundColor = [[UIColor whiteColor] colorWithAlphaComponent:0.34];
  handle_view.layer.cornerRadius = 3.0;
  [hero_header_card_view_ addSubview:handle_view];

  UILabel* sheet_title_label = [[[UILabel alloc] init] autorelease];
  sheet_title_label.translatesAutoresizingMaskIntoConstraints = NO;
  sheet_title_label.text = @"Compatibility";
  sheet_title_label.textColor = [XeniaTheme textPrimary];
  xe_apply_label_font(sheet_title_label, UIFontTextStyleTitle2, 24.0, UIFontWeightSemibold);
  [hero_header_card_view_ addSubview:sheet_title_label];

  UIButton* close_button = xe_make_ios_sheet_close_button(self, @selector(doneTapped:));
  [hero_header_card_view_ addSubview:close_button];

  UIStackView* content_stack = [[[UIStackView alloc] init] autorelease];
  content_stack.translatesAutoresizingMaskIntoConstraints = NO;
  content_stack.axis = UILayoutConstraintAxisVertical;
  content_stack.spacing = 8.0;
  [hero_header_card_view_ addSubview:content_stack];

  hero_title_label_ = [[UILabel alloc] init];
  hero_title_label_.translatesAutoresizingMaskIntoConstraints = NO;
  hero_title_label_.textColor = [XeniaTheme textPrimary];
  hero_title_label_.numberOfLines = 2;
  hero_title_label_.lineBreakMode = NSLineBreakByTruncatingTail;
  xe_apply_label_font(hero_title_label_, UIFontTextStyleLargeTitle, 30.0, UIFontWeightBold);
  [content_stack addArrangedSubview:hero_title_label_];

  hero_tid_label_ = [[UILabel alloc] init];
  hero_tid_label_.translatesAutoresizingMaskIntoConstraints = NO;
  hero_tid_label_.textColor = [XeniaTheme textSecondary];
  xe_apply_monospaced_label_font(hero_tid_label_, UIFontTextStyleBody, 14.0,
                                 UIFontWeightRegular);
  [content_stack addArrangedSubview:hero_tid_label_];

  hero_pills_stack_ = [[UIStackView alloc] init];
  hero_pills_stack_.translatesAutoresizingMaskIntoConstraints = NO;
  hero_pills_stack_.axis = UILayoutConstraintAxisHorizontal;
  hero_pills_stack_.spacing = 10.0;
  hero_pills_stack_.alignment = UIStackViewAlignmentCenter;
  [hero_pills_stack_ setContentHuggingPriority:UILayoutPriorityRequired
                                       forAxis:UILayoutConstraintAxisHorizontal];
  [hero_pills_stack_ setContentCompressionResistancePriority:UILayoutPriorityRequired
                                                     forAxis:UILayoutConstraintAxisHorizontal];

  hero_status_pill_ = [[XeniaPaddedLabel alloc] init];
  hero_status_pill_.translatesAutoresizingMaskIntoConstraints = NO;
  hero_status_pill_.padding = UIEdgeInsetsMake(2, 8, 2, 8);
  hero_status_pill_.textAlignment = NSTextAlignmentCenter;
  hero_status_pill_.layer.cornerRadius = 8.0;
  hero_status_pill_.clipsToBounds = YES;
  xe_apply_label_font(hero_status_pill_, UIFontTextStyleCaption1, 12.0, UIFontWeightMedium);
  [hero_status_pill_ setContentHuggingPriority:UILayoutPriorityRequired
                                       forAxis:UILayoutConstraintAxisHorizontal];
  [hero_status_pill_ setContentCompressionResistancePriority:UILayoutPriorityRequired
                                                     forAxis:UILayoutConstraintAxisHorizontal];
  hero_status_pill_.hidden = YES;

  hero_perf_pill_ = [[XeniaPaddedLabel alloc] init];
  hero_perf_pill_.translatesAutoresizingMaskIntoConstraints = NO;
  hero_perf_pill_.padding = UIEdgeInsetsMake(2, 8, 2, 8);
  hero_perf_pill_.textAlignment = NSTextAlignmentCenter;
  hero_perf_pill_.layer.cornerRadius = 8.0;
  hero_perf_pill_.clipsToBounds = YES;
  xe_apply_label_font(hero_perf_pill_, UIFontTextStyleCaption1, 12.0, UIFontWeightMedium);
  [hero_perf_pill_ setContentHuggingPriority:UILayoutPriorityRequired
                                     forAxis:UILayoutConstraintAxisHorizontal];
  [hero_perf_pill_ setContentCompressionResistancePriority:UILayoutPriorityRequired
                                                   forAxis:UILayoutConstraintAxisHorizontal];
  hero_perf_pill_.hidden = YES;

  [hero_pills_stack_ addArrangedSubview:hero_status_pill_];
  [hero_pills_stack_ addArrangedSubview:hero_perf_pill_];

  UIView* pills_row = [[[UIView alloc] init] autorelease];
  pills_row.translatesAutoresizingMaskIntoConstraints = NO;
  [pills_row addSubview:hero_pills_stack_];
  [NSLayoutConstraint activateConstraints:@[
    [hero_pills_stack_.topAnchor constraintEqualToAnchor:pills_row.topAnchor],
    [hero_pills_stack_.leadingAnchor constraintEqualToAnchor:pills_row.leadingAnchor],
    [hero_pills_stack_.bottomAnchor constraintEqualToAnchor:pills_row.bottomAnchor],
    [hero_pills_stack_.trailingAnchor constraintLessThanOrEqualToAnchor:pills_row.trailingAnchor],
  ]];
  [content_stack addArrangedSubview:pills_row];

  hero_updated_label_ = [[UILabel alloc] init];
  hero_updated_label_.translatesAutoresizingMaskIntoConstraints = NO;
  hero_updated_label_.textColor = [XeniaTheme textMuted];
  hero_updated_label_.numberOfLines = 1;
  xe_apply_label_font(hero_updated_label_, UIFontTextStyleFootnote, 13.0, UIFontWeightRegular);
  [content_stack addArrangedSubview:hero_updated_label_];

  [NSLayoutConstraint activateConstraints:@[
    [hero_header_card_view_.topAnchor constraintEqualToAnchor:hero_header_view_.topAnchor],
    [hero_header_card_view_.leadingAnchor constraintEqualToAnchor:hero_header_view_.leadingAnchor
                                                         constant:0.0],
    [hero_header_card_view_.trailingAnchor constraintEqualToAnchor:hero_header_view_.trailingAnchor
                                                          constant:0.0],
    [hero_header_card_view_.bottomAnchor constraintEqualToAnchor:hero_header_view_.bottomAnchor],
    [hero_header_backdrop_view_.topAnchor constraintEqualToAnchor:hero_header_card_view_.topAnchor],
    [hero_header_backdrop_view_.leadingAnchor
        constraintEqualToAnchor:hero_header_card_view_.leadingAnchor],
    [hero_header_backdrop_view_.trailingAnchor
        constraintEqualToAnchor:hero_header_card_view_.trailingAnchor],
    [hero_header_backdrop_view_.bottomAnchor
        constraintEqualToAnchor:hero_header_card_view_.bottomAnchor],
    [hero_header_blur_view_.topAnchor constraintEqualToAnchor:hero_header_card_view_.topAnchor],
    [hero_header_blur_view_.leadingAnchor
        constraintEqualToAnchor:hero_header_card_view_.leadingAnchor],
    [hero_header_blur_view_.trailingAnchor
        constraintEqualToAnchor:hero_header_card_view_.trailingAnchor],
    [hero_header_blur_view_.bottomAnchor
        constraintEqualToAnchor:hero_header_card_view_.bottomAnchor],
    [handle_view.topAnchor constraintEqualToAnchor:hero_header_card_view_.safeAreaLayoutGuide.topAnchor
                                          constant:8.0],
    [handle_view.centerXAnchor constraintEqualToAnchor:hero_header_card_view_.centerXAnchor],
    [handle_view.widthAnchor constraintEqualToConstant:60.0],
    [handle_view.heightAnchor constraintEqualToConstant:6.0],
    [sheet_title_label.topAnchor constraintEqualToAnchor:handle_view.bottomAnchor constant:16.0],
    [sheet_title_label.centerXAnchor constraintEqualToAnchor:hero_header_card_view_.centerXAnchor],
    [sheet_title_label.leadingAnchor constraintGreaterThanOrEqualToAnchor:hero_header_card_view_.leadingAnchor
                                                                 constant:72.0],
    [sheet_title_label.trailingAnchor constraintLessThanOrEqualToAnchor:close_button.leadingAnchor
                                                               constant:-12.0],
    [close_button.trailingAnchor constraintEqualToAnchor:hero_header_card_view_.trailingAnchor
                                                constant:-18.0],
    [close_button.centerYAnchor constraintEqualToAnchor:sheet_title_label.centerYAnchor],
    [content_stack.leadingAnchor constraintEqualToAnchor:hero_header_card_view_.leadingAnchor
                                                constant:28.0],
    [content_stack.trailingAnchor constraintEqualToAnchor:hero_header_card_view_.trailingAnchor
                                                 constant:-28.0],
    [content_stack.bottomAnchor constraintEqualToAnchor:hero_header_card_view_.bottomAnchor
                                               constant:-26.0],
    [content_stack.topAnchor constraintGreaterThanOrEqualToAnchor:sheet_title_label.bottomAnchor
                                                         constant:74.0],
  ]];

  [hero_header_view_ setNeedsLayout];
  [hero_header_view_ layoutIfNeeded];
  [self updateHeroHeaderContent];
  [self layoutHeroHeaderIfNeeded];
}

- (void)loadHeroArtwork {
  if (!title_id_) {
    return;
  }

  UIImage* cached_background = xe_cached_game_background_art(title_id_);
  if (cached_background) {
    [self setHeroBackgroundArtwork:cached_background];
  }

  if (!hero_artwork_) {
    UIImage* cached_cover = xe_cached_game_art(title_id_);
    if (cached_cover) {
      [self setHeroArtwork:cached_cover];
    }
  }

  if ((cached_background || hero_artwork_) && [self isViewLoaded]) {
    [self updateHeroHeaderContent];
  }

  const uint32_t expected_title_id = title_id_;
  if (!cached_background) {
    xe_fetch_game_background_art(expected_title_id, ^(UIImage* image) {
      if (!image || self->title_id_ != expected_title_id) {
        return;
      }
      [self setHeroBackgroundArtwork:image];
      if ([self isViewLoaded]) {
        [self updateHeroHeaderContent];
      }
    });
  }

  if (!hero_artwork_) {
    xe_fetch_game_art(expected_title_id, ^(UIImage* image) {
      if (!image || self->title_id_ != expected_title_id) {
        return;
      }
      [self setHeroArtwork:image];
      if ([self isViewLoaded]) {
        [self updateHeroHeaderContent];
      }
    });
  }
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = [XeniaTheme bgPrimary];
  self.tableView.backgroundColor = [UIColor clearColor];
  self.tableView.separatorStyle = UITableViewCellSeparatorStyleNone;
  self.tableView.rowHeight = UITableViewAutomaticDimension;
  self.tableView.estimatedRowHeight = 360.0;
  self.tableView.alwaysBounceVertical = YES;
  if (@available(iOS 11.0, *)) {
    self.tableView.contentInsetAdjustmentBehavior = UIScrollViewContentInsetAdjustmentNever;
  }
  if (@available(iOS 15.0, *)) {
    self.tableView.sectionHeaderTopPadding = 0;
  }
  self.title = @"";
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(onDiscussionDidUpdate:)
                                               name:kXeniaDiscussionDidUpdateNotification
                                             object:nil];
  [self buildHeroHeaderIfNeeded];
  [self loadHeroArtwork];
  [self fetchDiscussion];
}

- (void)viewWillAppear:(BOOL)animated {
  [super viewWillAppear:animated];
  [self.navigationController setNavigationBarHidden:YES animated:animated];
  [self layoutHeroHeaderIfNeeded];
  hero_background_view_.hidden = NO;
}

- (void)viewWillDisappear:(BOOL)animated {
  [super viewWillDisappear:animated];
  hero_background_view_.hidden = YES;
  [hero_background_view_ removeFromSuperview];
  [self.navigationController setNavigationBarHidden:NO animated:animated];
}

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
  [self layoutHeroHeaderIfNeeded];
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
  return UIInterfaceOrientationMaskAllButUpsideDown;
}

- (UIInterfaceOrientation)preferredInterfaceOrientationForPresentation {
  return UIInterfaceOrientationPortrait;
}

- (void)doneTapped:(id)__unused sender {
  hero_background_view_.hidden = YES;
  [hero_background_view_ removeFromSuperview];
  if (self.navigationController.presentingViewController &&
      self.navigationController.viewControllers.firstObject == self) {
    [self.navigationController dismissViewControllerAnimated:YES completion:nil];
    return;
  }
  [self.navigationController popViewControllerAnimated:YES];
}

- (void)submitReportTapped:(id)__unused sender {
  hero_background_view_.hidden = YES;
  [hero_background_view_ removeFromSuperview];
  XeniaCompatReportViewController* report_controller =
      [[XeniaCompatReportViewController alloc] initWithTitleID:title_id_ title:game_title_];
  [self.navigationController pushViewController:report_controller animated:YES];
  [report_controller release];
}

- (void)onDiscussionDidUpdate:(NSNotification*)notification {
  NSNumber* updated_title_id = notification.userInfo[@"titleId"];
  if (![updated_title_id isKindOfClass:[NSNumber class]] ||
      [updated_title_id unsignedIntValue] != title_id_) {
    return;
  }
  discussion_loading_ = YES;
  [self fetchDiscussion];
}

- (NSDictionary*)latestDiscussionReport {
  if (discussion_reports_.count == 0) {
    return nil;
  }
  id report = discussion_reports_.firstObject;
  return [report isKindOfClass:[NSDictionary class]] ? report : nil;
}

- (NSDictionary*)primaryCompatibilitySource {
  if (compat_info_) {
    return compat_info_;
  }
  return [self latestDiscussionReport];
}

- (void)applyDiscussionJSON:(NSDictionary*)json {
  [discussion_reports_ removeAllObjects];
  [discussion_expanded_report_indexes_ removeAllObjects];

  NSArray* raw_reports = json[@"reports"];
  if ([raw_reports isKindOfClass:[NSArray class]]) {
    for (id item in raw_reports) {
      if ([item isKindOfClass:[NSDictionary class]]) {
        [discussion_reports_ addObject:item];
      }
    }
  }

  [discussion_issue_url_ release];
  discussion_issue_url_ = nil;
  id issue_url = json[@"issueUrl"];
  if ([issue_url isKindOfClass:[NSString class]] && [issue_url length] > 0) {
    discussion_issue_url_ = [issue_url copy];
  }

  discussion_issue_number_ = 0;
  id issue_number = json[@"issueNumber"];
  if ([issue_number isKindOfClass:[NSNumber class]]) {
    discussion_issue_number_ = [issue_number integerValue];
  }

  if ((NSInteger)discussion_reports_.count <= kXeniaDiscussionPreviewCount) {
    discussion_show_all_ = NO;
  }
  if (discussion_reports_.count > 0) {
    [discussion_expanded_report_indexes_ addObject:@0];
  }
  [self updateHeroHeaderContent];
}

- (void)fetchDiscussion {
  NSString* cache_path = xe_discussion_cache_path(title_id_);
  NSData* cached_data = [NSData dataWithContentsOfFile:cache_path];
  if (cached_data.length > 0) {
    NSError* cache_error = nil;
    id cached_json = [NSJSONSerialization JSONObjectWithData:cached_data
                                                     options:0
                                                       error:&cache_error];
    if (!cache_error && [cached_json isKindOfClass:[NSDictionary class]]) {
      [self applyDiscussionJSON:(NSDictionary*)cached_json];
      discussion_loading_ = NO;
      [self.tableView reloadData];
    }
  }

  NSDictionary* cache_attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:cache_path
                                                                                    error:nil];
  NSDate* cache_modified_date = cache_attributes[NSFileModificationDate];
  if (cache_modified_date && [[NSDate date] timeIntervalSinceDate:cache_modified_date] < 300.0 &&
      !discussion_loading_) {
    return;
  }

  NSString* url_string = [NSString
      stringWithFormat:@"https://xenios-compat-api.xenios.workers.dev/games/%08X/discussion",
                       title_id_];
  NSURL* url = [NSURL URLWithString:url_string];
  if (!url) {
    discussion_loading_ = NO;
    [self.tableView reloadData];
    return;
  }

  NSURLSessionDataTask* task = [[NSURLSession sharedSession]
        dataTaskWithURL:url
      completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
        if (error || data.length == 0) {
          dispatch_async(dispatch_get_main_queue(), ^{
            self->discussion_loading_ = NO;
            [self.tableView reloadData];
          });
          return;
        }

        NSHTTPURLResponse* http_response = (NSHTTPURLResponse*)response;
        if (![http_response isKindOfClass:[NSHTTPURLResponse class]] ||
            http_response.statusCode != 200) {
          dispatch_async(dispatch_get_main_queue(), ^{
            self->discussion_loading_ = NO;
            [self.tableView reloadData];
          });
          return;
        }

        NSError* json_error = nil;
        id json = [NSJSONSerialization JSONObjectWithData:data options:0 error:&json_error];
        if (json_error || ![json isKindOfClass:[NSDictionary class]]) {
          dispatch_async(dispatch_get_main_queue(), ^{
            self->discussion_loading_ = NO;
            [self.tableView reloadData];
          });
          return;
        }

        [data writeToFile:cache_path atomically:YES];
        dispatch_async(dispatch_get_main_queue(), ^{
          self->discussion_loading_ = NO;
          [self applyDiscussionJSON:(NSDictionary*)json];
          [self.tableView reloadData];
        });
      }];
  [task resume];
}

- (void)viewIssueTapped:(id)__unused sender {
  if (!discussion_issue_url_) {
    return;
  }
  NSURL* url = [NSURL URLWithString:discussion_issue_url_];
  if (!url) {
    return;
  }
  [[UIApplication sharedApplication] openURL:url options:@{} completionHandler:nil];
}

- (void)toggleDiscussionExpansionTapped:(id)__unused sender {
  discussion_show_all_ = !discussion_show_all_;
  [self.tableView reloadData];
}

- (void)toggleDiscussionReportExpansionTapped:(UIButton*)sender {
  if (!sender) {
    return;
  }
  NSNumber* report_index = [NSNumber numberWithInteger:sender.tag];
  if ([discussion_expanded_report_indexes_ containsObject:report_index]) {
    [discussion_expanded_report_indexes_ removeObject:report_index];
  } else {
    [discussion_expanded_report_indexes_ addObject:report_index];
  }
  NSIndexPath* discussion_path = [NSIndexPath indexPathForRow:0 inSection:0];
  [self.tableView reloadRowsAtIndexPaths:@[ discussion_path ]
                        withRowAnimation:UITableViewRowAnimationFade];
}

- (UIView*)cardViewForCell:(UITableViewCell*)cell {
  UIView* card = [[[UIView alloc] init] autorelease];
  card.translatesAutoresizingMaskIntoConstraints = NO;
  card.backgroundColor = [XeniaTheme bgSurface];
  card.layer.cornerRadius = XeniaRadiusXl;
  card.layer.borderWidth = 0.5;
  card.layer.borderColor = [XeniaTheme border].CGColor;
  [cell.contentView addSubview:card];
  [NSLayoutConstraint activateConstraints:@[
    [card.topAnchor constraintEqualToAnchor:cell.contentView.topAnchor constant:6],
    [card.leadingAnchor constraintEqualToAnchor:cell.contentView.leadingAnchor constant:16],
    [card.trailingAnchor constraintEqualToAnchor:cell.contentView.trailingAnchor constant:-16],
    [card.bottomAnchor constraintEqualToAnchor:cell.contentView.bottomAnchor constant:-6],
  ]];
  return card;
}

- (UIView*)detailsMetricTileWithLabel:(NSString*)label
                                value:(NSString*)value
                           valueColor:(UIColor*)value_color
                          valueIsPill:(BOOL)value_is_pill {
  UIView* tile = [[[UIView alloc] init] autorelease];
  tile.translatesAutoresizingMaskIntoConstraints = NO;
  tile.backgroundColor = [XeniaTheme bgPrimary];
  tile.layer.cornerRadius = XeniaRadiusMd;
  tile.layer.borderWidth = 0.5;
  tile.layer.borderColor = [XeniaTheme border].CGColor;

  UILabel* title = [[[UILabel alloc] init] autorelease];
  title.translatesAutoresizingMaskIntoConstraints = NO;
  title.text = label;
  title.font = [UIFont systemFontOfSize:11 weight:UIFontWeightSemibold];
  title.textColor = [XeniaTheme textMuted];
  [tile addSubview:title];

  UIView* value_view = nil;
  if (value_is_pill) {
    value_view = xe_make_tag_pill(value ?: @"Unknown", value_color ?: [XeniaTheme textMuted]);
  } else {
    UILabel* value_label = [[[UILabel alloc] init] autorelease];
    value_label.translatesAutoresizingMaskIntoConstraints = NO;
    value_label.text = value ?: @"Unknown";
    value_label.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];
    value_label.textColor = value_color ?: [XeniaTheme textPrimary];
    value_label.numberOfLines = 1;
    value_view = value_label;
  }
  [tile addSubview:value_view];

  [NSLayoutConstraint activateConstraints:@[
    [tile.heightAnchor constraintGreaterThanOrEqualToConstant:86.0],
    [title.topAnchor constraintEqualToAnchor:tile.topAnchor constant:12.0],
    [title.leadingAnchor constraintEqualToAnchor:tile.leadingAnchor constant:12.0],
    [title.trailingAnchor constraintLessThanOrEqualToAnchor:tile.trailingAnchor constant:-12.0],
    [value_view.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:10.0],
    [value_view.leadingAnchor constraintEqualToAnchor:tile.leadingAnchor constant:12.0],
    [value_view.trailingAnchor constraintLessThanOrEqualToAnchor:tile.trailingAnchor
                                                        constant:-12.0],
    [value_view.bottomAnchor constraintLessThanOrEqualToAnchor:tile.bottomAnchor constant:-12.0],
  ]];

  return tile;
}

- (UITableViewCell*)detailsCellForTableView:(UITableView*)__unused tableView {
  UITableViewCell* cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                                  reuseIdentifier:nil] autorelease];
  cell.selectionStyle = UITableViewCellSelectionStyleNone;
  cell.backgroundColor = [UIColor clearColor];
  cell.contentView.backgroundColor = [UIColor clearColor];

  NSDictionary* best_report = [self latestDiscussionReport];
  NSString* status = [best_report[@"status"] isKindOfClass:[NSString class]]
                         ? best_report[@"status"]
                         : compat_info_[@"status"];
  NSString* status_label = status.length > 0 ? xe_compat_status_label(status) : @"Unknown";
  UIColor* status_color =
      status.length > 0 ? xe_compat_status_color(status) : [XeniaTheme textMuted];

  NSString* report_device = [best_report[@"deviceMachine"] isKindOfClass:[NSString class]]
                                ? best_report[@"deviceMachine"]
                                : best_report[@"device"];
  NSString* device =
      report_device.length > 0 ? xe_device_display_name_for_machine(report_device) : @"Unknown";

  NSString* platform_display =
      xe_platform_display_text(best_report[@"platform"], best_report[@"osVersion"]);
  if (platform_display.length == 0) {
    platform_display = @"Unknown";
  }

  NSString* gpu = [best_report[@"gpuBackend"] isKindOfClass:[NSString class]]
                      ? best_report[@"gpuBackend"]
                      : nil;
  if (gpu.length == 0) {
    gpu = @"Unknown";
  } else {
    gpu = [gpu uppercaseString];
  }

  NSString* based_on_date =
      [best_report[@"date"] isKindOfClass:[NSString class]] ? best_report[@"date"] : nil;
  if (based_on_date.length == 0 && [compat_info_[@"updatedAt"] isKindOfClass:[NSString class]]) {
    based_on_date = compat_info_[@"updatedAt"];
  }
  NSString* footnote = @"Based on available compatibility data.";
  if (based_on_date.length > 0) {
    footnote = [NSString
        stringWithFormat:@"Based on the latest report from %@.", xe_format_iso_date(based_on_date)];
  }

  UIView* card = [self cardViewForCell:cell];

  UILabel* heading = [[[UILabel alloc] init] autorelease];
  heading.translatesAutoresizingMaskIntoConstraints = NO;
  heading.text = @"Details";
  heading.font = [UIFont systemFontOfSize:17 weight:UIFontWeightSemibold];
  heading.textColor = [XeniaTheme textPrimary];
  [card addSubview:heading];

  UILabel* subheading = [[[UILabel alloc] init] autorelease];
  subheading.translatesAutoresizingMaskIntoConstraints = NO;
  subheading.text = @"BEST RESULT";
  subheading.font = [UIFont systemFontOfSize:12 weight:UIFontWeightSemibold];
  subheading.textColor = [XeniaTheme textMuted];
  [card addSubview:subheading];

  UIStackView* grid = [[[UIStackView alloc] init] autorelease];
  grid.translatesAutoresizingMaskIntoConstraints = NO;
  grid.axis = UILayoutConstraintAxisVertical;
  grid.spacing = 10.0;
  [card addSubview:grid];

  UIStackView* row_one = [[[UIStackView alloc] init] autorelease];
  row_one.axis = UILayoutConstraintAxisHorizontal;
  row_one.spacing = 10.0;
  row_one.distribution = UIStackViewDistributionFillEqually;
  [grid addArrangedSubview:row_one];

  UIStackView* row_two = [[[UIStackView alloc] init] autorelease];
  row_two.axis = UILayoutConstraintAxisHorizontal;
  row_two.spacing = 10.0;
  row_two.distribution = UIStackViewDistributionFillEqually;
  [grid addArrangedSubview:row_two];

  [row_one addArrangedSubview:[self detailsMetricTileWithLabel:@"STATUS"
                                                         value:status_label
                                                    valueColor:status_color
                                                   valueIsPill:YES]];
  [row_one addArrangedSubview:[self detailsMetricTileWithLabel:@"DEVICE"
                                                         value:device
                                                    valueColor:[XeniaTheme textPrimary]
                                                   valueIsPill:NO]];
  [row_two addArrangedSubview:[self detailsMetricTileWithLabel:@"PLATFORM"
                                                         value:platform_display
                                                    valueColor:[XeniaTheme textPrimary]
                                                   valueIsPill:NO]];
  [row_two addArrangedSubview:[self detailsMetricTileWithLabel:@"GPU"
                                                         value:gpu
                                                    valueColor:[XeniaTheme textPrimary]
                                                   valueIsPill:NO]];

  UILabel* footer = [[[UILabel alloc] init] autorelease];
  footer.translatesAutoresizingMaskIntoConstraints = NO;
  footer.text = footnote;
  footer.font = [UIFont systemFontOfSize:12];
  footer.textColor = [XeniaTheme textSecondary];
  footer.numberOfLines = 0;
  [card addSubview:footer];

  [NSLayoutConstraint activateConstraints:@[
    [heading.topAnchor constraintEqualToAnchor:card.topAnchor constant:16.0],
    [heading.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:16.0],
    [heading.trailingAnchor constraintEqualToAnchor:card.trailingAnchor constant:-16.0],
    [subheading.topAnchor constraintEqualToAnchor:heading.bottomAnchor constant:12.0],
    [subheading.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:16.0],
    [subheading.trailingAnchor constraintEqualToAnchor:card.trailingAnchor constant:-16.0],
    [grid.topAnchor constraintEqualToAnchor:subheading.bottomAnchor constant:10.0],
    [grid.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:16.0],
    [grid.trailingAnchor constraintEqualToAnchor:card.trailingAnchor constant:-16.0],
    [footer.topAnchor constraintEqualToAnchor:grid.bottomAnchor constant:12.0],
    [footer.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:16.0],
    [footer.trailingAnchor constraintEqualToAnchor:card.trailingAnchor constant:-16.0],
    [footer.bottomAnchor constraintEqualToAnchor:card.bottomAnchor constant:-14.0],
  ]];

  return cell;
}

- (UITableViewCell*)ctaCellForTableView:(UITableView*)__unused tableView {
  UITableViewCell* cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                                  reuseIdentifier:nil] autorelease];
  cell.selectionStyle = UITableViewCellSelectionStyleNone;
  cell.backgroundColor = [UIColor clearColor];
  cell.contentView.backgroundColor = [UIColor clearColor];

  UIView* card = [[[UIView alloc] init] autorelease];
  card.translatesAutoresizingMaskIntoConstraints = NO;
  card.backgroundColor = [[XeniaTheme accent] colorWithAlphaComponent:0.04];
  card.layer.cornerRadius = XeniaRadiusXl;
  card.layer.borderWidth = 1.0;
  card.layer.borderColor = [[XeniaTheme accent] colorWithAlphaComponent:0.20].CGColor;
  card.clipsToBounds = YES;
  [cell.contentView addSubview:card];

  UILabel* heading = [[[UILabel alloc] init] autorelease];
  heading.translatesAutoresizingMaskIntoConstraints = NO;
  heading.text = @"Tested this game?";
  heading.font = [UIFont systemFontOfSize:17 weight:UIFontWeightSemibold];
  heading.textColor = [XeniaTheme textPrimary];
  heading.textAlignment = NSTextAlignmentCenter;
  [card addSubview:heading];

  UILabel* subtext = [[[UILabel alloc] init] autorelease];
  subtext.translatesAutoresizingMaskIntoConstraints = NO;
  subtext.text = @"Help the community by sharing how well this title runs on your device.";
  subtext.font = [UIFont systemFontOfSize:14];
  subtext.textColor = [XeniaTheme textSecondary];
  subtext.numberOfLines = 0;
  subtext.textAlignment = NSTextAlignmentCenter;
  [card addSubview:subtext];

  UIButton* submit_button = [UIButton buttonWithType:UIButtonTypeSystem];
  submit_button.translatesAutoresizingMaskIntoConstraints = NO;
  [submit_button setTitle:@"Submit Report" forState:UIControlStateNormal];
  submit_button.titleLabel.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];
  [submit_button setTitleColor:[XeniaTheme accentFg] forState:UIControlStateNormal];
  submit_button.backgroundColor = [XeniaTheme accent];
  submit_button.layer.cornerRadius = XeniaRadiusMd;
  submit_button.clipsToBounds = YES;
  [submit_button addTarget:self
                    action:@selector(submitReportTapped:)
          forControlEvents:UIControlEventTouchUpInside];
  [card addSubview:submit_button];

  [NSLayoutConstraint activateConstraints:@[
    [card.topAnchor constraintEqualToAnchor:cell.contentView.topAnchor constant:6.0],
    [card.leadingAnchor constraintEqualToAnchor:cell.contentView.leadingAnchor constant:16.0],
    [card.trailingAnchor constraintEqualToAnchor:cell.contentView.trailingAnchor constant:-16.0],
    [card.bottomAnchor constraintEqualToAnchor:cell.contentView.bottomAnchor constant:-6.0],
    [heading.topAnchor constraintEqualToAnchor:card.topAnchor constant:18.0],
    [heading.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:16.0],
    [heading.trailingAnchor constraintEqualToAnchor:card.trailingAnchor constant:-16.0],
    [subtext.topAnchor constraintEqualToAnchor:heading.bottomAnchor constant:8.0],
    [subtext.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:24.0],
    [subtext.trailingAnchor constraintEqualToAnchor:card.trailingAnchor constant:-24.0],
    [submit_button.topAnchor constraintEqualToAnchor:subtext.bottomAnchor constant:16.0],
    [submit_button.centerXAnchor constraintEqualToAnchor:card.centerXAnchor],
    [submit_button.heightAnchor constraintEqualToConstant:44.0],
    [submit_button.widthAnchor constraintGreaterThanOrEqualToConstant:164.0],
    [submit_button.leadingAnchor constraintGreaterThanOrEqualToAnchor:card.leadingAnchor
                                                             constant:16.0],
    [submit_button.trailingAnchor constraintLessThanOrEqualToAnchor:card.trailingAnchor
                                                           constant:-16.0],
    [submit_button.bottomAnchor constraintEqualToAnchor:card.bottomAnchor constant:-18.0],
  ]];

  return cell;
}

- (UIView*)discussionPreviewCardForReport:(NSDictionary*)report reportIndex:(NSInteger)report_index {
  NSString* author = [report[@"submittedBy"] isKindOfClass:[NSString class]] ? report[@"submittedBy"]
                                                                             : @"anonymous";
  NSString* notes =
      [report[@"notes"] isKindOfClass:[NSString class]] ? report[@"notes"] : @"";
  NSString* date_string =
      [report[@"date"] isKindOfClass:[NSString class]] ? report[@"date"] : @"";
  NSString* formatted_date =
      date_string.length >= 10 ? xe_format_iso_date(date_string) : date_string;

  if (![notes isKindOfClass:[NSString class]] || notes.length == 0) {
    notes = @"No details provided.";
  }

  NSMutableArray<NSString*>* info_parts = [NSMutableArray array];
  NSString* status = [report[@"status"] isKindOfClass:[NSString class]] ? report[@"status"] : nil;
  if (status.length > 0) {
    [info_parts addObject:xe_compat_status_label(status)];
  }
  NSString* report_device = [report[@"deviceMachine"] isKindOfClass:[NSString class]]
                                ? report[@"deviceMachine"]
                                : report[@"device"];
  if ([report_device isKindOfClass:[NSString class]] && report_device.length > 0) {
    [info_parts addObject:xe_device_display_name_for_machine(report_device)];
  }
  NSString* platform_display = xe_platform_display_text(report[@"platform"], report[@"osVersion"]);
  if (platform_display.length > 0) {
    [info_parts addObject:platform_display];
  }
  NSString* gpu_backend =
      [report[@"gpuBackend"] isKindOfClass:[NSString class]] ? report[@"gpuBackend"] : nil;
  if (gpu_backend.length > 0) {
    [info_parts addObject:[gpu_backend uppercaseString]];
  }
  NSString* info_text = [info_parts componentsJoinedByString:@" \u00B7 "];

  UIView* card = [[[UIView alloc] init] autorelease];
  card.translatesAutoresizingMaskIntoConstraints = NO;
  card.backgroundColor = [XeniaTheme bgPrimary];
  card.layer.cornerRadius = XeniaRadiusMd;
  card.layer.borderWidth = 0.5;
  card.layer.borderColor = [XeniaTheme border].CGColor;

  UILabel* author_label = [[[UILabel alloc] init] autorelease];
  author_label.translatesAutoresizingMaskIntoConstraints = NO;
  author_label.text = author.length > 0 ? author : @"anonymous";
  author_label.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];
  author_label.textColor = [XeniaTheme textPrimary];
  [card addSubview:author_label];

  UILabel* date_label = [[[UILabel alloc] init] autorelease];
  date_label.translatesAutoresizingMaskIntoConstraints = NO;
  date_label.text = formatted_date;
  date_label.font = [UIFont systemFontOfSize:13];
  date_label.textColor = [XeniaTheme textMuted];
  date_label.textAlignment = NSTextAlignmentRight;
  [date_label setContentHuggingPriority:UILayoutPriorityRequired
                                forAxis:UILayoutConstraintAxisHorizontal];
  [date_label setContentCompressionResistancePriority:UILayoutPriorityRequired
                                              forAxis:UILayoutConstraintAxisHorizontal];
  [card addSubview:date_label];

  UILabel* info_label = [[[UILabel alloc] init] autorelease];
  info_label.translatesAutoresizingMaskIntoConstraints = NO;
  info_label.text = info_text;
  info_label.font = [UIFont systemFontOfSize:13];
  info_label.textColor = [XeniaTheme textMuted];
  info_label.numberOfLines = 2;
  info_label.hidden = info_label.text.length == 0;
  [card addSubview:info_label];

  UILabel* notes_label = [[[UILabel alloc] init] autorelease];
  notes_label.translatesAutoresizingMaskIntoConstraints = NO;
  notes_label.text = notes;
  notes_label.font = [UIFont systemFontOfSize:15];
  notes_label.textColor = [XeniaTheme textSecondary];
  BOOL report_expanded =
      [discussion_expanded_report_indexes_ containsObject:[NSNumber numberWithInteger:report_index]];
  notes_label.numberOfLines = report_expanded ? 0 : 3;
  notes_label.lineBreakMode = report_expanded ? NSLineBreakByWordWrapping
                                              : NSLineBreakByTruncatingTail;
  [card addSubview:notes_label];

  BOOL can_expand_notes = notes.length > 170 || [notes rangeOfString:@"\n"].location != NSNotFound;
  UIButton* expand_notes_button = [UIButton buttonWithType:UIButtonTypeSystem];
  expand_notes_button.translatesAutoresizingMaskIntoConstraints = NO;
  [expand_notes_button setTitle:(report_expanded ? @"Show less" : @"Show more")
                       forState:UIControlStateNormal];
  [expand_notes_button setTitleColor:[XeniaTheme accent] forState:UIControlStateNormal];
  expand_notes_button.titleLabel.font = [UIFont systemFontOfSize:12 weight:UIFontWeightSemibold];
  expand_notes_button.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
  expand_notes_button.tag = report_index;
  expand_notes_button.hidden = !can_expand_notes;
  [expand_notes_button addTarget:self
                          action:@selector(toggleDiscussionReportExpansionTapped:)
                forControlEvents:UIControlEventTouchUpInside];
  [card addSubview:expand_notes_button];

  UIButton* open_comment_button = [UIButton buttonWithType:UIButtonTypeSystem];
  open_comment_button.translatesAutoresizingMaskIntoConstraints = NO;
  [open_comment_button setTitle:@"Open comment" forState:UIControlStateNormal];
  [open_comment_button setTitleColor:[XeniaTheme accent] forState:UIControlStateNormal];
  open_comment_button.titleLabel.font = [UIFont systemFontOfSize:13 weight:UIFontWeightSemibold];
  open_comment_button.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
  open_comment_button.hidden = (discussion_issue_url_ == nil);
  [open_comment_button addTarget:self
                          action:@selector(viewIssueTapped:)
                forControlEvents:UIControlEventTouchUpInside];
  [card addSubview:open_comment_button];

  [NSLayoutConstraint activateConstraints:@[
    [author_label.topAnchor constraintEqualToAnchor:card.topAnchor constant:12],
    [author_label.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:10],
    [date_label.firstBaselineAnchor constraintEqualToAnchor:author_label.firstBaselineAnchor],
    [date_label.trailingAnchor constraintEqualToAnchor:card.trailingAnchor constant:-10],
    [date_label.leadingAnchor constraintGreaterThanOrEqualToAnchor:author_label.trailingAnchor
                                                          constant:8],
    [notes_label.topAnchor constraintEqualToAnchor:author_label.bottomAnchor constant:6],
    [notes_label.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:10],
    [notes_label.trailingAnchor constraintEqualToAnchor:card.trailingAnchor constant:-10],
    [expand_notes_button.topAnchor constraintEqualToAnchor:notes_label.bottomAnchor constant:4],
    [expand_notes_button.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:10],
    [expand_notes_button.trailingAnchor constraintLessThanOrEqualToAnchor:card.trailingAnchor
                                                                 constant:-10],
    [info_label.topAnchor constraintEqualToAnchor:expand_notes_button.bottomAnchor constant:6],
    [info_label.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:10],
    [info_label.trailingAnchor constraintEqualToAnchor:card.trailingAnchor constant:-10],
    [open_comment_button.topAnchor constraintEqualToAnchor:info_label.bottomAnchor constant:8],
    [open_comment_button.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:10],
    [open_comment_button.trailingAnchor constraintLessThanOrEqualToAnchor:card.trailingAnchor
                                                                 constant:-10],
    [open_comment_button.bottomAnchor constraintEqualToAnchor:card.bottomAnchor constant:-10],
  ]];
  if (expand_notes_button.hidden) {
    [expand_notes_button.heightAnchor constraintEqualToConstant:0].active = YES;
  }
  if (info_label.hidden) {
    [info_label.heightAnchor constraintEqualToConstant:0].active = YES;
  }
  if (open_comment_button.hidden) {
    [open_comment_button.heightAnchor constraintEqualToConstant:0].active = YES;
  }

  return card;
}

- (UITableViewCell*)discussionPreviewCellForTableView:(UITableView*)__unused tableView {
  UITableViewCell* cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                                  reuseIdentifier:nil] autorelease];
  cell.selectionStyle = UITableViewCellSelectionStyleNone;
  cell.backgroundColor = [UIColor clearColor];
  cell.contentView.backgroundColor = [UIColor clearColor];

  UIView* card = [self cardViewForCell:cell];
  UIStackView* stack = [[[UIStackView alloc] init] autorelease];
  stack.translatesAutoresizingMaskIntoConstraints = NO;
  stack.axis = UILayoutConstraintAxisVertical;
  stack.spacing = 12.0;
  [card addSubview:stack];
  [NSLayoutConstraint activateConstraints:@[
    [stack.topAnchor constraintEqualToAnchor:card.topAnchor constant:16],
    [stack.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:16],
    [stack.trailingAnchor constraintEqualToAnchor:card.trailingAnchor constant:-16],
    [stack.bottomAnchor constraintEqualToAnchor:card.bottomAnchor constant:-16],
  ]];

  UIView* heading_row = [[[UIView alloc] init] autorelease];
  heading_row.translatesAutoresizingMaskIntoConstraints = NO;
  heading_row.backgroundColor = [XeniaTheme bgSurface];
  UILabel* heading_label = [[[UILabel alloc] init] autorelease];
  heading_label.translatesAutoresizingMaskIntoConstraints = NO;
  heading_label.text = @"Discussion";
  heading_label.font = [UIFont systemFontOfSize:17 weight:UIFontWeightSemibold];
  heading_label.textColor = [XeniaTheme textPrimary];
  [heading_row addSubview:heading_label];

  UIButton* heading_button = [UIButton buttonWithType:UIButtonTypeSystem];
  heading_button.translatesAutoresizingMaskIntoConstraints = NO;
  NSString* button_title =
      discussion_issue_number_ > 0
          ? [NSString stringWithFormat:@"View Issue #%ld", (long)discussion_issue_number_]
          : @"View on GitHub";
  [heading_button setTitle:button_title forState:UIControlStateNormal];
  [heading_button setTitleColor:[XeniaTheme accent] forState:UIControlStateNormal];
  heading_button.titleLabel.font = [UIFont systemFontOfSize:14 weight:UIFontWeightMedium];
  heading_button.hidden = (discussion_issue_url_ == nil);
  [heading_button addTarget:self
                     action:@selector(viewIssueTapped:)
           forControlEvents:UIControlEventTouchUpInside];
  [heading_row addSubview:heading_button];

  [NSLayoutConstraint activateConstraints:@[
    [heading_label.topAnchor constraintEqualToAnchor:heading_row.topAnchor],
    [heading_label.leadingAnchor constraintEqualToAnchor:heading_row.leadingAnchor],
    [heading_label.bottomAnchor constraintEqualToAnchor:heading_row.bottomAnchor],
    [heading_button.firstBaselineAnchor constraintEqualToAnchor:heading_label.firstBaselineAnchor],
    [heading_button.trailingAnchor constraintEqualToAnchor:heading_row.trailingAnchor],
    [heading_button.leadingAnchor constraintGreaterThanOrEqualToAnchor:heading_label.trailingAnchor
                                                              constant:8],
  ]];
  [stack addArrangedSubview:heading_row];

  if (discussion_loading_) {
    UILabel* loading_label = [[[UILabel alloc] init] autorelease];
    loading_label.text = @"Loading discussion...";
    loading_label.font = [UIFont systemFontOfSize:15];
    loading_label.textColor = [XeniaTheme textMuted];
    loading_label.numberOfLines = 1;
    [stack addArrangedSubview:loading_label];
    return cell;
  }

  if (discussion_reports_.count == 0) {
    UILabel* empty_label = [[[UILabel alloc] init] autorelease];
    empty_label.text = @"No reports yet. Be the first to submit one.";
    empty_label.font = [UIFont systemFontOfSize:15];
    empty_label.textColor = [XeniaTheme textMuted];
    empty_label.numberOfLines = 0;
    [stack addArrangedSubview:empty_label];
    return cell;
  }

  NSInteger report_count = (NSInteger)discussion_reports_.count;
  NSInteger visible_count =
      discussion_show_all_ ? report_count : MIN(report_count, kXeniaDiscussionPreviewCount);
  for (NSInteger report_index = 0; report_index < visible_count; ++report_index) {
    NSDictionary* report = discussion_reports_[report_index];
    if (![report isKindOfClass:[NSDictionary class]]) {
      continue;
    }
    [stack addArrangedSubview:[self discussionPreviewCardForReport:report reportIndex:report_index]];
  }

  if (report_count > kXeniaDiscussionPreviewCount) {
    UILabel* summary_label = [[[UILabel alloc] init] autorelease];
    summary_label.text =
        discussion_show_all_
            ? [NSString stringWithFormat:@"Showing all %ld reports.", (long)report_count]
            : [NSString stringWithFormat:@"Showing latest %ld of %ld reports.", (long)visible_count,
                                         (long)report_count];
    summary_label.font = [UIFont systemFontOfSize:12];
    summary_label.textColor = [XeniaTheme textMuted];
    summary_label.numberOfLines = 1;
    [stack addArrangedSubview:summary_label];

    UIButton* toggle_button = [UIButton buttonWithType:UIButtonTypeSystem];
    toggle_button.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
    NSString* toggle_title =
        discussion_show_all_
            ? @"Show fewer reports"
            : [NSString stringWithFormat:@"Show all %ld reports", (long)report_count];
    [toggle_button setTitle:toggle_title forState:UIControlStateNormal];
    [toggle_button setTitleColor:[XeniaTheme accent] forState:UIControlStateNormal];
    toggle_button.titleLabel.font = [UIFont systemFontOfSize:13 weight:UIFontWeightSemibold];
    [toggle_button addTarget:self
                      action:@selector(toggleDiscussionExpansionTapped:)
            forControlEvents:UIControlEventTouchUpInside];
    [stack addArrangedSubview:toggle_button];
  }

  return cell;
}

#pragma mark - UITableViewDataSource

- (NSInteger)numberOfSectionsInTableView:(UITableView* __unused)tableView {
  return 1;
}

- (NSInteger)tableView:(UITableView* __unused)tableView
    numberOfRowsInSection:(NSInteger)__unused section {
  return 3;
}

- (UITableViewCell*)tableView:(UITableView*)tableView
        cellForRowAtIndexPath:(NSIndexPath*)indexPath {
  if (indexPath.row == 0) {
    return [self discussionPreviewCellForTableView:tableView];
  }
  if (indexPath.row == 1) {
    return [self detailsCellForTableView:tableView];
  }
  return [self ctaCellForTableView:tableView];
}

@end

@implementation XeniaCompatReportViewController {
  uint32_t title_id_;
  NSString* game_title_;
  NSInteger selected_status_;
  NSInteger selected_perf_;
  UITextView* notes_text_view_;
  NSMutableArray<UIImage*>* screenshots_;
  BOOL submitting_;
}

- (instancetype)initWithTitleID:(uint32_t)title_id title:(NSString*)title {
  self = [super initWithStyle:UITableViewStyleInsetGrouped];
  if (self) {
    title_id_ = title_id;
    game_title_ = [title copy];
    selected_status_ = -1;
    selected_perf_ = -1;
    screenshots_ = [[NSMutableArray alloc] init];
    submitting_ = NO;
    self.title = @"Submit Report";
  }
  return self;
}

- (void)dealloc {
  [game_title_ release];
  [notes_text_view_ release];
  [screenshots_ release];
  [super dealloc];
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.tableView.backgroundColor = [UIColor systemBackgroundColor];
  self.tableView.rowHeight = UITableViewAutomaticDimension;
  self.tableView.estimatedRowHeight = 64.0;
  if (@available(iOS 15.0, *)) {
    self.tableView.sectionHeaderTopPadding = 0;
  }
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
  return UIInterfaceOrientationMaskAllButUpsideDown;
}

- (UIInterfaceOrientation)preferredInterfaceOrientationForPresentation {
  return UIInterfaceOrientationPortrait;
}

- (void)dismissKeyboard {
  [notes_text_view_ resignFirstResponder];
}

- (void)showAlertWithTitle:(NSString*)title message:(NSString*)message {
  xe_present_ok_alert(self, title, message);
}

- (void)finishSuccessfulSubmissionWithIssueURL:(NSString*)issue_url {
  [[NSFileManager defaultManager] removeItemAtPath:xe_discussion_cache_path(title_id_) error:nil];
  [[NSNotificationCenter defaultCenter] postNotificationName:kXeniaDiscussionDidUpdateNotification
                                                      object:nil
                                                    userInfo:@{@"titleId" : @(title_id_)}];

  NSString* message = @"Your compatibility report has been submitted.";
  if ([issue_url isKindOfClass:[NSString class]] && issue_url.length > 0) {
    message = [message stringByAppendingFormat:@"\n\nGitHub issue: %@", issue_url];
  }

  UIAlertController* alert =
      [UIAlertController alertControllerWithTitle:@"Report Submitted"
                                          message:message
                                   preferredStyle:UIAlertControllerStyleAlert];
  [alert addAction:[UIAlertAction
                       actionWithTitle:@"OK"
                                 style:UIAlertActionStyleDefault
                               handler:^(__unused UIAlertAction* action) {
                                 if (self.navigationController) {
                                   [self.navigationController popViewControllerAnimated:YES];
                                 } else {
                                   [self dismissViewControllerAnimated:YES completion:nil];
                                 }
                               }]];
  [self presentViewController:alert animated:YES completion:nil];
}

- (NSString*)trimmedNotes {
  return [notes_text_view_.text
      stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

- (void)submitReport {
  if (submitting_) {
    return;
  }
  if (selected_status_ < 0) {
    [self showAlertWithTitle:@"Missing Status" message:@"Please select a compatibility status."];
    return;
  }
  if (selected_perf_ < 0) {
    [self showAlertWithTitle:@"Missing Performance" message:@"Please select a performance tier."];
    return;
  }

  NSString* notes = [self trimmedNotes];
  if (notes.length == 0) {
    [self showAlertWithTitle:@"Missing Notes"
                     message:@"Please add a short note about your experience."];
    return;
  }

  submitting_ = YES;
  [self.tableView reloadSections:[NSIndexSet indexSetWithIndex:6]
                withRowAnimation:UITableViewRowAnimationNone];

  NSMutableArray<NSString*>* screenshot_data =
      [NSMutableArray arrayWithCapacity:screenshots_.count];
  NSUInteger screenshot_total_bytes = 0;
  for (UIImage* image in screenshots_) {
    NSData* jpeg = UIImageJPEGRepresentation(image, 0.8);
    if (!jpeg) {
      continue;
    }
    screenshot_total_bytes += jpeg.length;
    [screenshot_data addObject:[jpeg base64EncodedStringWithOptions:0]];
  }

  NSString* device_machine = xe_device_machine();
  NSString* device_display = xe_device_display_name();
  NSDictionary* payload = @{
    @"titleId" : [NSString stringWithFormat:@"%08X", title_id_],
    @"title" : game_title_ ?: @"",
    @"status" : xe_compat_statuses()[selected_status_],
    @"perf" : xe_compat_perfs()[selected_perf_],
    @"platform" : @"ios",
    @"device" : device_display ?: @"Unknown",
    @"deviceMachine" : device_machine ?: @"Unknown",
    @"osVersion" : [UIDevice currentDevice].systemVersion ?: @"",
    @"arch" : @"arm64",
    @"gpuBackend" : @"msl",
    @"notes" : notes,
    @"tags" : @[],
    @"screenshots" : screenshot_data,
  };

  NSError* json_error = nil;
  NSData* request_body = [NSJSONSerialization dataWithJSONObject:payload
                                                         options:0
                                                           error:&json_error];
  if (!request_body) {
    submitting_ = NO;
    [self.tableView reloadSections:[NSIndexSet indexSetWithIndex:6]
                  withRowAnimation:UITableViewRowAnimationNone];
    NSString* message =
        json_error.localizedDescription ?: @"Unable to serialize the report payload.";
    [self showAlertWithTitle:@"Submission Failed" message:message];
    return;
  }

  XELOGI("iOS compat submit: title_id={:08X} status={} perf={} screenshots={} "
         "screenshot_bytes={} body_bytes={}",
         title_id_, [xe_compat_statuses()[selected_status_] UTF8String],
         [xe_compat_perfs()[selected_perf_] UTF8String], (int)screenshot_data.count,
         static_cast<unsigned long long>(screenshot_total_bytes),
         static_cast<unsigned long long>(request_body.length));

  NSURL* url = [NSURL URLWithString:@"https://xenios-compat-api.xenios.workers.dev/report"];
  NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
  request.HTTPMethod = @"POST";
  [request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
  [request setValue:@"Bearer xenios-compat-report" forHTTPHeaderField:@"Authorization"];
  request.HTTPBody = request_body;

  NSURLSessionDataTask* task = [[NSURLSession sharedSession]
      dataTaskWithRequest:request
        completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
          dispatch_async(dispatch_get_main_queue(), ^{
            self->submitting_ = NO;
            [self.tableView reloadSections:[NSIndexSet indexSetWithIndex:6]
                          withRowAnimation:UITableViewRowAnimationNone];

            if (error) {
              [self showAlertWithTitle:@"Network Error" message:error.localizedDescription];
              return;
            }

            NSHTTPURLResponse* http_response = (NSHTTPURLResponse*)response;
            NSInteger status_code = [http_response isKindOfClass:[NSHTTPURLResponse class]]
                                        ? http_response.statusCode
                                        : 0;

            NSString* response_text = @"";
            if (data.length > 0) {
              NSString* body_text =
                  [[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] autorelease];
              if (body_text.length > 0) {
                response_text = body_text;
              }
            }

            if (![http_response isKindOfClass:[NSHTTPURLResponse class]] || status_code < 200 ||
                status_code >= 300) {
              NSString* message =
                  [NSString stringWithFormat:@"Server returned HTTP %ld", (long)status_code];
              if (response_text.length > 0) {
                message = [message stringByAppendingFormat:@"\n%@", response_text];
              }
              [self showAlertWithTitle:@"Submission Failed" message:message];
              return;
            }

            NSString* issue_url = nil;
            if (data.length > 0) {
              id response_json = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
              if ([response_json isKindOfClass:[NSDictionary class]] &&
                  [response_json[@"issueUrl"] isKindOfClass:[NSString class]]) {
                issue_url = response_json[@"issueUrl"];
              }
            }

            [self finishSuccessfulSubmissionWithIssueURL:issue_url];
          });
        }];
  [task resume];
}

- (void)addScreenshotTapped {
  if (screenshots_.count >= 5) {
    [self showAlertWithTitle:@"Limit Reached" message:@"You can attach up to 5 screenshots."];
    return;
  }

  PHPickerConfiguration* configuration = [[PHPickerConfiguration alloc] init];
  configuration.selectionLimit = static_cast<NSInteger>(5 - screenshots_.count);
  configuration.filter = [PHPickerFilter imagesFilter];

  PHPickerViewController* picker =
      [[PHPickerViewController alloc] initWithConfiguration:configuration];
  picker.delegate = self;
  [self presentViewController:picker animated:YES completion:nil];
  [picker release];
  [configuration release];
}

#pragma mark - PHPickerViewControllerDelegate

- (void)picker:(PHPickerViewController*)picker didFinishPicking:(NSArray<PHPickerResult*>*)results {
  [picker dismissViewControllerAnimated:YES completion:nil];

  for (PHPickerResult* result in results) {
    if (screenshots_.count >= 5) {
      break;
    }
    [result.itemProvider
        loadObjectOfClass:[UIImage class]
        completionHandler:^(id<NSItemProviderReading> object, NSError* __unused error) {
          UIImage* image = (UIImage*)object;
          if (!image) {
            return;
          }

          // Re-render to strip metadata and keep uploads bounded.
          CGFloat max_dimension = 1280.0;
          CGSize size = image.size;
          CGFloat scale = 1.0;
          if (size.width > max_dimension || size.height > max_dimension) {
            scale = (size.width > size.height) ? (max_dimension / size.width)
                                               : (max_dimension / size.height);
          }
          CGSize new_size = CGSizeMake(size.width * scale, size.height * scale);
          UIGraphicsBeginImageContextWithOptions(new_size, NO, 1.0);
          [image drawInRect:CGRectMake(0, 0, new_size.width, new_size.height)];
          UIImage* sanitized_image = UIGraphicsGetImageFromCurrentImageContext();
          UIGraphicsEndImageContext();
          if (!sanitized_image) {
            return;
          }

          dispatch_async(dispatch_get_main_queue(), ^{
            if (self->screenshots_.count >= 5) {
              return;
            }
            [self->screenshots_ addObject:sanitized_image];
            [self.tableView reloadSections:[NSIndexSet indexSetWithIndex:5]
                          withRowAnimation:UITableViewRowAnimationAutomatic];
          });
        }];
  }
}

#pragma mark - UITableViewDataSource

- (NSInteger)numberOfSectionsInTableView:(UITableView* __unused)tableView {
  return 7;
}

- (NSInteger)tableView:(UITableView* __unused)tableView numberOfRowsInSection:(NSInteger)section {
  switch (section) {
    case 0:
      return 2;
    case 1:
      return 4;
    case 2:
      return (NSInteger)xe_compat_statuses().count;
    case 3:
      return (NSInteger)xe_compat_perfs().count;
    case 4:
      return 1;
    case 5:
      return static_cast<NSInteger>(screenshots_.count) + 1;
    case 6:
      return 1;
    default:
      return 0;
  }
}

- (NSString*)tableView:(UITableView* __unused)tableView titleForHeaderInSection:(NSInteger)section {
  switch (section) {
    case 0:
      return @"Game";
    case 1:
      return @"Device";
    case 2:
      return @"Compatibility Status";
    case 3:
      return @"Performance";
    case 4:
      return @"Notes";
    case 5:
      return @"Screenshots";
    default:
      return nil;
  }
}

- (CGFloat)tableView:(UITableView* __unused)tableView
    heightForRowAtIndexPath:(NSIndexPath*)indexPath {
  if (indexPath.section == 4) {
    return 128.0;
  }
  if (indexPath.section == 6) {
    return 52.0;
  }
  return UITableViewAutomaticDimension;
}

- (UITableViewCell*)tableView:(UITableView*)tableView
        cellForRowAtIndexPath:(NSIndexPath*)indexPath {
  if (indexPath.section == 0 || indexPath.section == 1) {
    UITableViewCell* cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1
                                                    reuseIdentifier:nil] autorelease];
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    if (indexPath.section == 0) {
      if (indexPath.row == 0) {
        cell.textLabel.text = @"Title";
        cell.detailTextLabel.text = game_title_;
      } else {
        cell.textLabel.text = @"Title ID";
        cell.detailTextLabel.text = [NSString stringWithFormat:@"%08X", title_id_];
      }
    } else {
      switch (indexPath.row) {
        case 0:
          cell.textLabel.text = @"Device";
          cell.detailTextLabel.text = xe_device_display_name();
          break;
        case 1:
          cell.textLabel.text = @"OS Version";
          cell.detailTextLabel.text = [UIDevice currentDevice].systemVersion;
          break;
        case 2:
          cell.textLabel.text = @"Architecture";
          cell.detailTextLabel.text = @"arm64";
          break;
        case 3:
          cell.textLabel.text = @"GPU Backend";
          cell.detailTextLabel.text = @"msl";
          break;
      }
    }
    return cell;
  }

  if (indexPath.section == 2 || indexPath.section == 3) {
    UITableViewCell* cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                                    reuseIdentifier:nil] autorelease];
    cell.tintColor = [XeniaTheme accent];
    cell.textLabel.text = @"";
    NSArray<NSString*>* keys = indexPath.section == 2 ? xe_compat_statuses() : xe_compat_perfs();
    NSArray<NSString*>* labels =
        indexPath.section == 2 ? xe_compat_status_labels() : xe_compat_perf_labels();
    NSString* key = keys[indexPath.row];
    NSString* label = labels[indexPath.row];
    UIColor* color =
        indexPath.section == 2 ? xe_compat_status_color(key) : xe_compat_perf_color(key);
    XeniaPaddedLabel* pill = xe_make_tag_pill(label, color);
    [cell.contentView addSubview:pill];
    [NSLayoutConstraint activateConstraints:@[
      [pill.leadingAnchor constraintEqualToAnchor:cell.contentView.leadingAnchor constant:16],
      [pill.centerYAnchor constraintEqualToAnchor:cell.contentView.centerYAnchor],
    ]];

    if (indexPath.section == 2) {
      cell.accessoryType = indexPath.row == selected_status_ ? UITableViewCellAccessoryCheckmark
                                                             : UITableViewCellAccessoryNone;
    } else {
      BOOL force_na = (selected_status_ == 4);
      BOOL enabled = !force_na || indexPath.row == 3;
      cell.accessoryType = indexPath.row == selected_perf_ ? UITableViewCellAccessoryCheckmark
                                                           : UITableViewCellAccessoryNone;
      cell.selectionStyle =
          enabled ? UITableViewCellSelectionStyleDefault : UITableViewCellSelectionStyleNone;
      pill.alpha = enabled ? 1.0 : 0.35;
    }
    return cell;
  }

  if (indexPath.section == 4) {
    UITableViewCell* cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                                    reuseIdentifier:nil] autorelease];
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    if (!notes_text_view_) {
      notes_text_view_ = [[UITextView alloc] init];
      notes_text_view_.backgroundColor = [UIColor clearColor];
      notes_text_view_.textColor = [XeniaTheme textPrimary];
      notes_text_view_.textContainerInset = UIEdgeInsetsMake(8, 4, 8, 4);
      xe_apply_text_view_font(notes_text_view_, UIFontTextStyleBody, 15.0, UIFontWeightRegular,
                              NO);

      UIToolbar* keyboard_toolbar =
          [[[UIToolbar alloc] initWithFrame:CGRectMake(0, 0, 320, 44)] autorelease];
      [keyboard_toolbar sizeToFit];
      UIBarButtonItem* spacer =
          [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace
                                                         target:nil
                                                         action:nil] autorelease];
      UIBarButtonItem* done =
          [[[UIBarButtonItem alloc] initWithTitle:@"Done"
                                            style:UIBarButtonItemStyleDone
                                           target:self
                                           action:@selector(dismissKeyboard)] autorelease];
      done.tintColor = [XeniaTheme accent];
      keyboard_toolbar.items = @[ spacer, done ];
      notes_text_view_.inputAccessoryView = keyboard_toolbar;
    }

    if (notes_text_view_.superview != cell.contentView) {
      notes_text_view_.translatesAutoresizingMaskIntoConstraints = NO;
      [cell.contentView addSubview:notes_text_view_];
      [NSLayoutConstraint activateConstraints:@[
        [notes_text_view_.topAnchor constraintEqualToAnchor:cell.contentView.topAnchor constant:8],
        [notes_text_view_.bottomAnchor constraintEqualToAnchor:cell.contentView.bottomAnchor
                                                      constant:-8],
        [notes_text_view_.leadingAnchor constraintEqualToAnchor:cell.contentView.leadingAnchor
                                                       constant:8],
        [notes_text_view_.trailingAnchor constraintEqualToAnchor:cell.contentView.trailingAnchor
                                                        constant:-8],
      ]];
    }
    return cell;
  }

  if (indexPath.section == 5) {
    if (indexPath.row < static_cast<NSInteger>(screenshots_.count)) {
      UITableViewCell* cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                                      reuseIdentifier:nil] autorelease];
      cell.selectionStyle = UITableViewCellSelectionStyleNone;
      UIImage* thumbnail = screenshots_[indexPath.row];
      cell.imageView.image = thumbnail;
      cell.imageView.contentMode = UIViewContentModeScaleAspectFill;
      cell.imageView.clipsToBounds = YES;
      cell.imageView.layer.cornerRadius = 4.0;
      cell.textLabel.text =
          [NSString stringWithFormat:@"Screenshot %ld", (long)(indexPath.row + 1)];
      cell.textLabel.textColor = [XeniaTheme textPrimary];
      return cell;
    }

    UITableViewCell* cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                                    reuseIdentifier:nil] autorelease];
    cell.textLabel.text = @"Add Screenshot";
    cell.textLabel.textColor = [XeniaTheme accent];
    cell.imageView.image = [UIImage systemImageNamed:@"plus.circle"];
    cell.imageView.tintColor = [XeniaTheme accent];
    return cell;
  }

  UITableViewCell* cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                                  reuseIdentifier:nil] autorelease];
  cell.backgroundColor = [XeniaTheme accent];
  cell.clipsToBounds = YES;
  cell.layer.cornerRadius = XeniaRadiusMd;
  cell.textLabel.text = submitting_ ? @"Submitting..." : @"Submit Report";
  cell.textLabel.textColor = [XeniaTheme accentFg];
  cell.textLabel.textAlignment = NSTextAlignmentCenter;
  xe_apply_label_font(cell.textLabel, UIFontTextStyleHeadline, 17.0, UIFontWeightSemibold);
  return cell;
}

#pragma mark - UITableViewDelegate

- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)indexPath {
  [tableView deselectRowAtIndexPath:indexPath animated:YES];

  if (indexPath.section == 2) {
    selected_status_ = indexPath.row;
    if (selected_status_ == 4) {
      selected_perf_ = 3;
    }
    [tableView reloadSections:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(2, 2)]
             withRowAnimation:UITableViewRowAnimationNone];
    return;
  }

  if (indexPath.section == 3) {
    if (selected_status_ == 4 && indexPath.row != 3) {
      return;
    }
    selected_perf_ = indexPath.row;
    [tableView reloadSections:[NSIndexSet indexSetWithIndex:3]
             withRowAnimation:UITableViewRowAnimationNone];
    return;
  }

  if (indexPath.section == 4) {
    [notes_text_view_ becomeFirstResponder];
    return;
  }

  if (indexPath.section == 5) {
    if (indexPath.row >= static_cast<NSInteger>(screenshots_.count)) {
      [self addScreenshotTapped];
    }
    return;
  }

  if (indexPath.section == 6) {
    [self submitReport];
  }
}

- (BOOL)tableView:(UITableView* __unused)tableView canEditRowAtIndexPath:(NSIndexPath*)indexPath {
  return (indexPath.section == 5 && indexPath.row < static_cast<NSInteger>(screenshots_.count));
}

- (void)tableView:(UITableView*)tableView
    commitEditingStyle:(UITableViewCellEditingStyle)editingStyle
     forRowAtIndexPath:(NSIndexPath*)indexPath {
  if (editingStyle != UITableViewCellEditingStyleDelete) {
    return;
  }
  if (indexPath.section != 5 || indexPath.row >= static_cast<NSInteger>(screenshots_.count)) {
    return;
  }
  [screenshots_ removeObjectAtIndex:indexPath.row];
  [tableView reloadSections:[NSIndexSet indexSetWithIndex:5]
           withRowAnimation:UITableViewRowAnimationAutomatic];
}

@end

@implementation XeniaLogViewController {
  UITextView* textView_;
  UILabel* footerLabel_;
  NSTimer* autoRefreshTimer_;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.title = @"Live Log";
  self.view.backgroundColor = [UIColor systemBackgroundColor];

  UIBarButtonItem* refresh_button =
      [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemRefresh
                                                    target:self
                                                    action:@selector(reloadLogTapped:)];
  UIBarButtonItem* share_button =
      [[UIBarButtonItem alloc] initWithTitle:@"Share"
                                       style:UIBarButtonItemStylePlain
                                      target:self
                                      action:@selector(shareLogTapped:)];
  self.navigationItem.rightBarButtonItems = @[ refresh_button, share_button ];

  textView_ = [[UITextView alloc] init];
  textView_.translatesAutoresizingMaskIntoConstraints = NO;
  textView_.editable = NO;
  textView_.selectable = YES;
  textView_.alwaysBounceVertical = YES;
  textView_.backgroundColor = [UIColor secondarySystemBackgroundColor];
  textView_.font = [UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightRegular];
  textView_.textContainerInset = UIEdgeInsetsMake(12, 12, 12, 12);
  [self.view addSubview:textView_];

  footerLabel_ = [[UILabel alloc] init];
  footerLabel_.translatesAutoresizingMaskIntoConstraints = NO;
  footerLabel_.textColor = [XeniaTheme textSecondary];
  footerLabel_.font = [UIFont systemFontOfSize:12];
  footerLabel_.numberOfLines = 2;
  [self.view addSubview:footerLabel_];

  UILayoutGuide* guide = self.view.safeAreaLayoutGuide;
  [NSLayoutConstraint activateConstraints:@[
    [textView_.topAnchor constraintEqualToAnchor:guide.topAnchor constant:8],
    [textView_.leadingAnchor constraintEqualToAnchor:guide.leadingAnchor constant:10],
    [textView_.trailingAnchor constraintEqualToAnchor:guide.trailingAnchor constant:-10],
    [textView_.bottomAnchor constraintEqualToAnchor:footerLabel_.topAnchor constant:-8],
    [footerLabel_.leadingAnchor constraintEqualToAnchor:guide.leadingAnchor constant:12],
    [footerLabel_.trailingAnchor constraintEqualToAnchor:guide.trailingAnchor constant:-12],
    [footerLabel_.bottomAnchor constraintEqualToAnchor:guide.bottomAnchor constant:-8],
  ]];
}

- (void)viewWillAppear:(BOOL)animated {
  [super viewWillAppear:animated];
  [self updateCloseButton];
  [self reloadLog];
}

- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
  [self startAutoRefresh];
}

- (void)viewWillDisappear:(BOOL)animated {
  [super viewWillDisappear:animated];
  [self stopAutoRefresh];
}

- (void)dealloc {
  [self stopAutoRefresh];
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
  return UIInterfaceOrientationMaskAllButUpsideDown;
}

- (UIInterfaceOrientation)preferredInterfaceOrientationForPresentation {
  return UIInterfaceOrientationPortrait;
}

- (void)reloadLogTapped:(id)sender {
  [self reloadLog];
}

- (void)closeTapped:(id)sender {
  [self dismissViewControllerAnimated:YES completion:nil];
}

- (BOOL)shouldShowCloseButton {
  UINavigationController* nav = self.navigationController;
  if (!nav || nav.presentingViewController == nil) {
    return NO;
  }
  return nav.viewControllers.count > 0 && nav.viewControllers.firstObject == self;
}

- (void)updateCloseButton {
  if ([self shouldShowCloseButton]) {
    self.navigationItem.leftBarButtonItem =
        [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                                                      target:self
                                                      action:@selector(closeTapped:)];
  } else {
    self.navigationItem.leftBarButtonItem = nil;
  }
}

- (void)startAutoRefresh {
  if (autoRefreshTimer_) {
    return;
  }
  autoRefreshTimer_ = [NSTimer scheduledTimerWithTimeInterval:0.5
                                                        target:self
                                                      selector:@selector(autoRefreshTick:)
                                                      userInfo:nil
                                                       repeats:YES];
  autoRefreshTimer_.tolerance = 0.15;
}

- (void)stopAutoRefresh {
  if (!autoRefreshTimer_) {
    return;
  }
  [autoRefreshTimer_ invalidate];
  autoRefreshTimer_ = nil;
}

- (void)autoRefreshTick:(NSTimer* __unused)timer {
  [self reloadLog];
}

- (BOOL)handleControllerActions:(const xe::ui::apple::ControllerActionSet&)actions {
  if (actions.context) {
    [self shareLogTapped:nil];
    return YES;
  }
  if (actions.quick_action) {
    [self reloadLogTapped:nil];
    return YES;
  }
  if (!(actions.navigate_up || actions.navigate_down || actions.page_prev ||
        actions.page_next)) {
    return NO;
  }

  CGFloat step = MAX(40.0, CGRectGetHeight(textView_.bounds) * 0.45);
  CGFloat offset = textView_.contentOffset.y;
  if (actions.navigate_up) {
    offset -= step;
  }
  if (actions.navigate_down) {
    offset += step;
  }
  if (actions.page_prev) {
    offset -= CGRectGetHeight(textView_.bounds);
  }
  if (actions.page_next) {
    offset += CGRectGetHeight(textView_.bounds);
  }
  CGFloat max_offset = MAX(0.0, textView_.contentSize.height - CGRectGetHeight(textView_.bounds));
  offset = MIN(MAX(0.0, offset), max_offset);
  [textView_ setContentOffset:CGPointMake(0.0, offset) animated:YES];
  return YES;
}

- (BOOL)isNearBottom {
  CGFloat content_height = textView_.contentSize.height;
  CGFloat visible_height = CGRectGetHeight(textView_.bounds);
  if (content_height <= visible_height + 1.0) {
    return YES;
  }
  CGFloat bottom = textView_.contentOffset.y + visible_height;
  return bottom >= content_height - 36.0;
}

- (void)shareLogTapped:(id)sender {
  std::filesystem::path log_path = GetLogFilePath();
  NSString* log_path_ns = ToNSString(log_path.string());
  if (![[NSFileManager defaultManager] fileExistsAtPath:log_path_ns]) {
    UIAlertController* alert =
        [UIAlertController alertControllerWithTitle:@"Log Not Found"
                                            message:@"No xenia.log file exists yet."
                                     preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK"
                                              style:UIAlertActionStyleDefault
                                            handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
    return;
  }

  NSURL* log_url = [NSURL fileURLWithPath:log_path_ns];
  UIActivityViewController* share_controller =
      [[UIActivityViewController alloc] initWithActivityItems:@[ log_url ]
                                        applicationActivities:nil];
  UIPopoverPresentationController* popover = share_controller.popoverPresentationController;
  if (popover) {
    popover.barButtonItem = self.navigationItem.rightBarButtonItems.lastObject;
  }
  [self presentViewController:share_controller animated:YES completion:nil];
}

- (void)reloadLog {
  static constexpr size_t kMaxLogBytes = 256 * 1024;
  BOOL should_follow_tail = [self isNearBottom];
  std::filesystem::path log_path = GetLogFilePath();
  std::string content = ReadFileTail(log_path, kMaxLogBytes);
  NSString* log_path_ns = ToNSString(log_path.string());

  NSString* display_text = nil;
  if (content.empty()) {
    display_text =
        [NSString stringWithFormat:@"No recent log data found.\n\nExpected file:\n%@\n\n"
                                   @"Launch a game and keep this open. This view updates "
                                   @"automatically.",
                                   log_path_ns];
  } else {
    display_text = DecodeLogBytesToNSString(content);
  }

  if (!display_text) {
    display_text = @"";
  }
  if (![textView_.text isEqualToString:display_text]) {
    textView_.text = display_text;
  }
  if (should_follow_tail) {
    [textView_ scrollRangeToVisible:NSMakeRange(textView_.text.length, 0)];
  }

  footerLabel_.text =
      [NSString stringWithFormat:@"Live tail (0.5s). Showing last %zu KB from %@",
                                 kMaxLogBytes / 1024, log_path_ns];
}

@end

@implementation XeniaLandscapeNavigationController

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
  return UIInterfaceOrientationMaskAllButUpsideDown;
}

- (UIInterfaceOrientation)preferredInterfaceOrientationForPresentation {
  return UIInterfaceOrientationPortrait;
}

- (BOOL)shouldAutorotate {
  return YES;
}

@end

@implementation XeniaConfigViewController {
  std::vector<IOSConfigSection> sections_;
  BOOL hasPendingChanges_;
  UIBarButtonItem* saveButton_;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.title = @"Settings";
  self.tableView.backgroundColor = [UIColor systemBackgroundColor];
  self.tableView.separatorInset = UIEdgeInsetsMake(0, 16, 0, 16);
  sections_ = BuildIOSConfigSections();
  hasPendingChanges_ = NO;

  self.navigationItem.leftBarButtonItem =
      [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemCancel
                                                    target:self
                                                    action:@selector(cancelTapped:)];
  saveButton_ = [[UIBarButtonItem alloc] initWithTitle:@"Save"
                                                 style:UIBarButtonItemStyleDone
                                                target:self
                                                action:@selector(saveTapped:)];
  saveButton_.enabled = NO;
  self.navigationItem.rightBarButtonItem = saveButton_;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
  return UIInterfaceOrientationMaskAllButUpsideDown;
}

- (UIInterfaceOrientation)preferredInterfaceOrientationForPresentation {
  return UIInterfaceOrientationPortrait;
}

- (void)markPendingChanges {
  hasPendingChanges_ = YES;
  saveButton_.enabled = YES;
}

- (IOSConfigItem*)itemAtIndexPath:(NSIndexPath*)indexPath {
  if (indexPath.section < 0 || indexPath.section >= static_cast<NSInteger>(sections_.size())) {
    return nullptr;
  }
  IOSConfigSection& section = sections_[indexPath.section];
  if (indexPath.row < 0 || indexPath.row >= static_cast<NSInteger>(section.items.size())) {
    return nullptr;
  }
  return &section.items[indexPath.row];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView*)tableView {
  return static_cast<NSInteger>(sections_.size());
}

- (NSInteger)tableView:(UITableView*)tableView numberOfRowsInSection:(NSInteger)section {
  if (section < 0 || section >= static_cast<NSInteger>(sections_.size())) {
    return 0;
  }
  return static_cast<NSInteger>(sections_[section].items.size());
}

- (NSString*)tableView:(UITableView*)tableView titleForHeaderInSection:(NSInteger)section {
  if (section < 0 || section >= static_cast<NSInteger>(sections_.size())) {
    return nil;
  }
  return ToNSString(sections_[section].title);
}

- (NSString*)tableView:(UITableView*)tableView titleForFooterInSection:(NSInteger)section {
  if (section < 0 || section >= static_cast<NSInteger>(sections_.size())) {
    return nil;
  }
  return ToNSString(sections_[section].footer);
}

- (UITableViewCell*)tableView:(UITableView*)tableView
        cellForRowAtIndexPath:(NSIndexPath*)indexPath {
  static NSString* const kCellIdentifier = @"XeniaConfigCell";
  UITableViewCell* cell = [tableView dequeueReusableCellWithIdentifier:kCellIdentifier];
  if (!cell) {
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                  reuseIdentifier:kCellIdentifier];
  }

  IOSConfigItem* item = [self itemAtIndexPath:indexPath];
  if (!item) {
    cell.textLabel.text = @"";
    cell.detailTextLabel.text = @"";
    cell.accessoryType = UITableViewCellAccessoryNone;
    cell.accessoryView = nil;
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    return cell;
  }

  cell.textLabel.text = ToNSString(item->title);
  cell.detailTextLabel.textColor = [XeniaTheme textSecondary];
  cell.detailTextLabel.numberOfLines = 2;
  cell.textLabel.numberOfLines = 1;

  if (item->control_type == IOSConfigControlType::kToggle) {
    cell.textLabel.textColor = [XeniaTheme textPrimary];
    cell.detailTextLabel.text = ToNSString(item->subtitle);
    UISwitch* toggle = [[UISwitch alloc] init];
    toggle.on = item->bool_value;
    [toggle addTarget:self
                  action:@selector(toggleChanged:)
        forControlEvents:UIControlEventValueChanged];
    cell.accessoryView = toggle;
    cell.accessoryType = UITableViewCellAccessoryNone;
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
  } else if (item->control_type == IOSConfigControlType::kAction) {
    cell.textLabel.textColor = self.view.tintColor;
    cell.detailTextLabel.text = ToNSString(item->subtitle);
    cell.accessoryView = nil;
    cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    cell.selectionStyle = UITableViewCellSelectionStyleDefault;
  } else {
    cell.textLabel.textColor = [XeniaTheme textPrimary];
    std::string value_title = ChoiceTitleForItem(*item);
    std::string subtitle = item->subtitle;
    if (!value_title.empty()) {
      cell.detailTextLabel.text = ToNSString(value_title + " · " + subtitle);
    } else {
      cell.detailTextLabel.text = ToNSString(subtitle);
    }
    cell.accessoryView = nil;
    cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    cell.selectionStyle = UITableViewCellSelectionStyleDefault;
  }

  return cell;
}

- (void)toggleChanged:(UISwitch*)sender {
  CGPoint point = [sender convertPoint:CGPointZero toView:self.tableView];
  NSIndexPath* indexPath = [self.tableView indexPathForRowAtPoint:point];
  if (!indexPath) {
    return;
  }
  IOSConfigItem* item = [self itemAtIndexPath:indexPath];
  if (!item || item->control_type != IOSConfigControlType::kToggle) {
    return;
  }
  item->bool_value = sender.isOn;
  [self markPendingChanges];
}

- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)indexPath {
  IOSConfigItem* item = [self itemAtIndexPath:indexPath];
  if (!item || item->control_type == IOSConfigControlType::kToggle) {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    return;
  }

  if (item->control_type == IOSConfigControlType::kAction) {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    switch (item->action) {
      case IOSConfigAction::kViewRecentLog: {
        XeniaLogViewController* log_vc = [[XeniaLogViewController alloc] init];
        [self.navigationController pushViewController:log_vc animated:YES];
      } break;
      case IOSConfigAction::kNone:
      default:
        break;
    }
    return;
  }

  XeniaChoiceListViewController* choice_vc = [[XeniaChoiceListViewController alloc]
      initWithTitle:ToNSString(item->title)
           subtitle:ToNSString(item->subtitle)
            choices:item->choices
      selectedValue:item->choice_value
        onSelection:^(int64_t selected_value) {
          item->choice_value = selected_value;
          [self markPendingChanges];
          [self.tableView reloadRowsAtIndexPaths:@[ indexPath ]
                                withRowAnimation:UITableViewRowAnimationNone];
        }];
  [self.navigationController pushViewController:choice_vc animated:YES];
  [tableView deselectRowAtIndexPath:indexPath animated:YES];
}

- (void)cancelTapped:(id)sender {
  if (!hasPendingChanges_) {
    [self dismissViewControllerAnimated:YES completion:nil];
    return;
  }

  UIAlertController* confirm =
      [UIAlertController alertControllerWithTitle:@"Discard Changes?"
                                          message:@"You have unsaved setting changes."
                                   preferredStyle:UIAlertControllerStyleAlert];
  [confirm addAction:[UIAlertAction actionWithTitle:@"Keep Editing"
                                              style:UIAlertActionStyleCancel
                                            handler:nil]];
  [confirm addAction:[UIAlertAction actionWithTitle:@"Discard"
                                              style:UIAlertActionStyleDestructive
                                            handler:^(__unused UIAlertAction* action) {
                                              [self dismissViewControllerAnimated:YES
                                                                       completion:nil];
                                            }]];
  [self presentViewController:confirm animated:YES completion:nil];
}

- (void)saveTapped:(id)sender {
  BOOL saved = ApplyIOSConfigSections(sections_) ? YES : NO;
  hasPendingChanges_ = NO;
  saveButton_.enabled = NO;

  NSString* title = saved ? @"Settings Saved" : @"Save Completed With Warnings";
  NSString* message = saved ? @"Saved to xenia-edge.config.toml."
                            : @"Some settings could not be applied. Check xenia.log.";
  UIAlertController* alert =
      [UIAlertController alertControllerWithTitle:title
                                          message:message
                                   preferredStyle:UIAlertControllerStyleAlert];
  [alert addAction:[UIAlertAction actionWithTitle:@"Done"
                                            style:UIAlertActionStyleDefault
                                          handler:^(__unused UIAlertAction* action) {
                                            [self dismissViewControllerAnimated:YES completion:nil];
                                          }]];
  [self presentViewController:alert animated:YES completion:nil];
}

@end

// ---------------------------------------------------------------------------
// XeniaViewController - manages the Metal view and game launcher UI.
// ---------------------------------------------------------------------------
@interface XeniaViewController : UIViewController <UIDocumentPickerDelegate,
                                                   UICollectionViewDataSource,
                                                   UICollectionViewDelegateFlowLayout,
                                                   XeniaGameContentHost>
@property(nonatomic, strong) XeniaMetalView* metalView;
@property(nonatomic, strong) UIView* launcherOverlay;
@property(nonatomic, strong) UIButton* openGameButton;
@property(nonatomic, strong) UIButton* settingsButton;
@property(nonatomic, strong) UIButton* profileButton;
@property(nonatomic, strong) UILabel* titleLabel;
@property(nonatomic, strong) UILabel* statusLabel;
@property(nonatomic, strong) UILabel* signedInProfileLabel;
@property(nonatomic, strong) UICollectionView* importedGamesCollectionView;
@property(nonatomic, strong) UILabel* importedGamesEmptyLabel;
@property(nonatomic, strong) UIView* inGameMenuOverlay;
@property(nonatomic, strong) UIButton* inGameResumeButton;
@property(nonatomic, strong) UIButton* inGameSettingsButton;
@property(nonatomic, strong) UIButton* inGameLiveLogButton;
@property(nonatomic, strong) UIButton* inGameExitButton;
@property(nonatomic, assign) BOOL gameRunning;
@property(nonatomic, assign) BOOL gameStopInProgress;
@property(nonatomic, assign) xe::ui::IOSWindowedAppContext* appContext;

// JIT status widgets.
@property(nonatomic, strong) UIView* jitWarningCard;
@property(nonatomic, strong) UIView* jitStatusDot;
@property(nonatomic, strong) UILabel* jitStatusLabel;
@property(nonatomic, strong) NSTimer* jitPollTimer;
@property(nonatomic, strong) NSTimer* controllerNavTimer;
@property(nonatomic, assign) BOOL jitAcquired;
@property(nonatomic, assign) BOOL launcherLandscapeUnlocked;
- (void)refreshSignedInProfileUI;
- (void)presentSystemSigninPromptForUserIndex:(uint32_t)user_index
                                   usersNeeded:(uint32_t)users_needed
                                    completion:(void (^)(BOOL success))completion;
- (void)presentSystemKeyboardPromptWithTitle:(NSString*)title
                                 description:(NSString*)description
                                 defaultText:(NSString*)default_text
                                  completion:(void (^)(BOOL cancelled,
                                                       NSString* text))completion;
- (void)setupInGameMenuOverlay;
- (void)toggleInGameMenuTapped:(UITapGestureRecognizer*)recognizer;
- (void)resumeGameTapped:(UIButton*)sender;
- (void)inGameSettingsTapped:(UIButton*)sender;
- (void)inGameLiveLogTapped:(UIButton*)sender;
- (void)exitGameTapped:(UIButton*)sender;
- (void)hideInGameMenuOverlay;
- (void)pollControllerNavigation:(NSTimer*)timer;
- (BOOL)readNativeControllerState:(xe::hid::X_INPUT_STATE*)out_state;
- (BOOL)handleExternalLaunchURL:(NSURL*)url;
- (void)startCompatFetchIfNeeded;
- (void)applyCompatDataToDiscoveredGames;
- (void)presentCompatibilitySheetForIndex:(size_t)game_index;
- (void)presentManageContentSheetForIndex:(size_t)game_index;
- (BOOL)installTitleUpdateAtPath:(NSString*)path
                          status:(NSString**)status_out
                  notTitleUpdate:(BOOL*)not_title_update_out;
@end

@implementation XeniaViewController {
  std::vector<IOSDiscoveredGame> discovered_games_;
  NSDictionary* compat_data_;
  xe::ui::apple::ControllerNavigationMapper controller_navigation_mapper_;
  xe::ui::apple::FocusGraph launcher_focus_graph_;
  xe::ui::apple::FocusGraph in_game_focus_graph_;
  NSInteger focused_game_index_;
  BOOL launcher_library_focus_active_;
  BOOL controller_navigation_was_enabled_;
  uint32_t native_controller_packet_number_;
  CGSize last_collection_layout_size_;
  BOOL compat_fetch_started_;
  std::filesystem::path pending_external_launch_path_;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = [UIColor blackColor];
  self.jitAcquired = NO;
  self.launcherLandscapeUnlocked = NO;
  self.gameRunning = NO;
  self.gameStopInProgress = NO;
  focused_game_index_ = -1;
  launcher_library_focus_active_ = NO;
  controller_navigation_was_enabled_ = NO;
  native_controller_packet_number_ = 0;
  last_collection_layout_size_ = CGSizeZero;
  compat_fetch_started_ = NO;
  controller_navigation_mapper_.Reset();

  // Create the Metal-backed rendering view (full screen, behind everything).
  self.metalView = [[XeniaMetalView alloc] initWithFrame:self.view.bounds];
  self.metalView.autoresizingMask =
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  self.metalView.contentScaleFactor = [UIScreen mainScreen].scale;
  [self.view addSubview:self.metalView];

  // Create the launcher overlay UI immediately. When JIT is missing, keep
  // settings/navigation available but gate game launch with status.
  [self setupLauncherOverlay];
  [self setupInGameMenuOverlay];
  UITapGestureRecognizer* tap = [[UITapGestureRecognizer alloc]
      initWithTarget:self
              action:@selector(toggleInGameMenuTapped:)];
  tap.numberOfTapsRequired = 1;
  tap.cancelsTouchesInView = NO;
  [self.view addGestureRecognizer:tap];
  [self updateJITStatusIndicator];
  [self updateJITAvailabilityUI];
  [self refreshSignedInProfileUI];
  NSDictionary* cached_compat_data = xe_load_cached_compat_data();
  if (cached_compat_data) {
    [compat_data_ release];
    compat_data_ = [cached_compat_data retain];
  }
  [self refreshImportedGames];

  // Start polling for JIT.
  [self startJITPoll];
  self.controllerNavTimer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 60.0)
                                                              target:self
                                                            selector:@selector(
                                                                         pollControllerNavigation:)
                                                            userInfo:nil
                                                             repeats:YES];
  self.controllerNavTimer.tolerance = 0.01;
}

- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
  [self startCompatFetchIfNeeded];
  xe_request_portrait_orientation(self);
  if (!self.launcherLandscapeUnlocked) {
    self.launcherLandscapeUnlocked = YES;
    [self setNeedsUpdateOfSupportedInterfaceOrientations];
  }
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
    [self->compat_data_ release];
    self->compat_data_ = [data retain];
    [self applyCompatDataToDiscoveredGames];
    [self.importedGamesCollectionView reloadData];
  });
}

- (void)setButton:(UIButton*)button controllerFocused:(BOOL)focused {
  if (!button) {
    return;
  }
  button.layer.cornerRadius = XeniaRadiusMd;
  button.layer.borderWidth = focused ? 1.5 : 0.0;
  button.layer.borderColor = focused ? [XeniaTheme accent].CGColor : [UIColor clearColor].CGColor;
  button.layer.shadowColor = [XeniaTheme accent].CGColor;
  button.layer.shadowOpacity = focused ? 0.35f : 0.0f;
  button.layer.shadowRadius = focused ? 6.0f : 0.0f;
  button.layer.shadowOffset = CGSizeZero;
}

- (void)setFocusedGameIndex:(NSInteger)index scroll:(BOOL)scroll {
  if (discovered_games_.empty()) {
    index = -1;
  } else {
    if (index < 0) {
      index = 0;
    }
    NSInteger max_index = static_cast<NSInteger>(discovered_games_.size() - 1);
    if (index > max_index) {
      index = max_index;
    }
  }
  NSInteger previous = focused_game_index_;
  focused_game_index_ = index;

  NSMutableArray<NSIndexPath*>* reload_paths = [NSMutableArray array];
  if (previous >= 0 && previous < static_cast<NSInteger>(discovered_games_.size())) {
    [reload_paths addObject:[NSIndexPath indexPathForItem:previous inSection:0]];
  }
  if (focused_game_index_ >= 0 &&
      focused_game_index_ < static_cast<NSInteger>(discovered_games_.size()) &&
      focused_game_index_ != previous) {
    [reload_paths addObject:[NSIndexPath indexPathForItem:focused_game_index_ inSection:0]];
  }
  if (reload_paths.count > 0) {
    [self.importedGamesCollectionView reloadItemsAtIndexPaths:reload_paths];
  }

  if (scroll && focused_game_index_ >= 0 &&
      focused_game_index_ < static_cast<NSInteger>(discovered_games_.size())) {
    NSIndexPath* path = [NSIndexPath indexPathForItem:focused_game_index_ inSection:0];
    [self.importedGamesCollectionView scrollToItemAtIndexPath:path
                                             atScrollPosition:UICollectionViewScrollPositionCenteredVertically
                                                     animated:YES];
  }
}

- (void)rebuildLauncherFocusGraph {
  IOSFocusNodeId previous_focus = launcher_focus_graph_.current();
  launcher_focus_graph_.Clear();

  xe::ui::apple::FocusNode settings;
  settings.id = kLauncherFocusSettings;
  settings.right = kLauncherFocusProfile;
  settings.down = kLauncherFocusImport;
  settings.enabled = self.settingsButton.enabled && !self.settingsButton.hidden;

  xe::ui::apple::FocusNode profile;
  profile.id = kLauncherFocusProfile;
  profile.left = kLauncherFocusSettings;
  profile.down = kLauncherFocusImport;
  profile.enabled = self.profileButton.enabled && !self.profileButton.hidden;

  xe::ui::apple::FocusNode import_button;
  import_button.id = kLauncherFocusImport;
  import_button.left = kLauncherFocusProfile;
  import_button.right = kLauncherFocusLibrary;
  import_button.up = kLauncherFocusSettings;
  import_button.down = kLauncherFocusLibrary;
  import_button.enabled = self.openGameButton.enabled && !self.openGameButton.hidden;

  xe::ui::apple::FocusNode library;
  library.id = kLauncherFocusLibrary;
  library.left = kLauncherFocusImport;
  library.up = kLauncherFocusImport;
  library.enabled = !discovered_games_.empty();

  launcher_focus_graph_.AddOrUpdateNode(settings);
  launcher_focus_graph_.AddOrUpdateNode(profile);
  launcher_focus_graph_.AddOrUpdateNode(import_button);
  launcher_focus_graph_.AddOrUpdateNode(library);

  if (previous_focus != xe::ui::apple::kInvalidFocusNodeId) {
    launcher_focus_graph_.SetCurrent(previous_focus);
  }
}

- (void)rebuildInGameFocusGraph {
  IOSFocusNodeId previous_focus = in_game_focus_graph_.current();
  in_game_focus_graph_.Clear();

  xe::ui::apple::FocusNode resume;
  resume.id = kInGameFocusResume;
  resume.down = kInGameFocusSettings;
  resume.enabled = self.inGameResumeButton && self.inGameResumeButton.enabled &&
                   !self.inGameResumeButton.hidden;

  xe::ui::apple::FocusNode settings;
  settings.id = kInGameFocusSettings;
  settings.up = kInGameFocusResume;
  settings.down = kInGameFocusLog;
  settings.enabled = self.inGameSettingsButton && self.inGameSettingsButton.enabled &&
                     !self.inGameSettingsButton.hidden;

  xe::ui::apple::FocusNode log;
  log.id = kInGameFocusLog;
  log.up = kInGameFocusSettings;
  log.down = kInGameFocusExit;
  log.enabled = self.inGameLiveLogButton && self.inGameLiveLogButton.enabled &&
                !self.inGameLiveLogButton.hidden;

  xe::ui::apple::FocusNode exit;
  exit.id = kInGameFocusExit;
  exit.up = kInGameFocusLog;
  exit.enabled = self.inGameExitButton && self.inGameExitButton.enabled &&
                 !self.inGameExitButton.hidden;

  in_game_focus_graph_.AddOrUpdateNode(resume);
  in_game_focus_graph_.AddOrUpdateNode(settings);
  in_game_focus_graph_.AddOrUpdateNode(log);
  in_game_focus_graph_.AddOrUpdateNode(exit);

  if (previous_focus != xe::ui::apple::kInvalidFocusNodeId) {
    in_game_focus_graph_.SetCurrent(previous_focus);
  }
}

- (void)applyLauncherFocusVisuals {
  if (!controller_navigation_was_enabled_) {
    launcher_library_focus_active_ = NO;
    [self setButton:self.settingsButton controllerFocused:NO];
    [self setButton:self.profileButton controllerFocused:NO];
    [self setButton:self.openGameButton controllerFocused:NO];
    if (focused_game_index_ >= 0 && focused_game_index_ < static_cast<NSInteger>(discovered_games_.size())) {
      NSIndexPath* focused_path = [NSIndexPath indexPathForItem:focused_game_index_ inSection:0];
      [self.importedGamesCollectionView reloadItemsAtIndexPaths:@[ focused_path ]];
    }
    return;
  }

  IOSFocusNodeId current_focus = launcher_focus_graph_.current();
  BOOL settings_focused = current_focus == kLauncherFocusSettings;
  BOOL profile_focused = current_focus == kLauncherFocusProfile;
  BOOL import_focused = current_focus == kLauncherFocusImport;
  BOOL library_focused = current_focus == kLauncherFocusLibrary;

  [self setButton:self.settingsButton controllerFocused:settings_focused];
  [self setButton:self.profileButton controllerFocused:profile_focused];
  [self setButton:self.openGameButton controllerFocused:import_focused];

  if (library_focused && focused_game_index_ < 0 && !discovered_games_.empty()) {
    [self setFocusedGameIndex:0 scroll:NO];
  }

  if (launcher_library_focus_active_ != library_focused &&
      focused_game_index_ >= 0 &&
      focused_game_index_ < static_cast<NSInteger>(discovered_games_.size())) {
    NSIndexPath* focused_path = [NSIndexPath indexPathForItem:focused_game_index_ inSection:0];
    [self.importedGamesCollectionView reloadItemsAtIndexPaths:@[ focused_path ]];
  }
  launcher_library_focus_active_ = library_focused;
}

- (void)applyInGameMenuFocusVisuals {
  if (!self.inGameMenuOverlay || self.inGameMenuOverlay.hidden || !controller_navigation_was_enabled_) {
    [self setButton:self.inGameResumeButton controllerFocused:NO];
    [self setButton:self.inGameSettingsButton controllerFocused:NO];
    [self setButton:self.inGameLiveLogButton controllerFocused:NO];
    [self setButton:self.inGameExitButton controllerFocused:NO];
    return;
  }

  IOSFocusNodeId current_focus = in_game_focus_graph_.current();
  [self setButton:self.inGameResumeButton controllerFocused:current_focus == kInGameFocusResume];
  [self setButton:self.inGameSettingsButton
  controllerFocused:current_focus == kInGameFocusSettings];
  [self setButton:self.inGameLiveLogButton controllerFocused:current_focus == kInGameFocusLog];
  [self setButton:self.inGameExitButton controllerFocused:current_focus == kInGameFocusExit];
}

- (NSInteger)launcherGridColumnCountForContentSize:(CGSize)content_size {
  CGFloat content_width = content_size.width;

  NSInteger columns = 2;
  if (content_width >= 1100.0f) {
    columns = 5;
  } else if (content_width >= 900.0f) {
    columns = 4;
  } else if (content_width >= 680.0f) {
    columns = 3;
  }
  return columns;
}

- (NSInteger)launcherGridColumnCount {
  return [self launcherGridColumnCountForContentSize:self.importedGamesCollectionView.bounds.size];
}

- (NSInteger)launcherPageStep {
  NSArray<NSIndexPath*>* visible = self.importedGamesCollectionView.indexPathsForVisibleItems;
  if (visible.count > 0) {
    return visible.count;
  }
  return 6;
}

- (BOOL)handleControllerActionsForTableController:(UITableViewController*)table_controller
                                          actions:(const xe::ui::apple::ControllerActionSet&)actions {
  UITableView* table_view = table_controller.tableView;
  if (!table_view) {
    return NO;
  }

  NSMutableArray<NSIndexPath*>* all_paths = [NSMutableArray array];
  NSInteger sections = [table_view numberOfSections];
  for (NSInteger section = 0; section < sections; ++section) {
    NSInteger rows = [table_view numberOfRowsInSection:section];
    for (NSInteger row = 0; row < rows; ++row) {
      [all_paths addObject:[NSIndexPath indexPathForRow:row inSection:section]];
    }
  }
  if (all_paths.count == 0) {
    return NO;
  }

  NSIndexPath* selected = table_view.indexPathForSelectedRow;
  NSInteger selected_index = 0;
  if (selected) {
    NSUInteger found = [all_paths indexOfObject:selected];
    if (found != NSNotFound) {
      selected_index = static_cast<NSInteger>(found);
    }
  } else {
    selected = all_paths.firstObject;
    [table_view selectRowAtIndexPath:selected
                            animated:NO
                      scrollPosition:UITableViewScrollPositionMiddle];
  }

  BOOL handled = NO;
  if (actions.navigate_up && selected_index > 0) {
    selected_index--;
    handled = YES;
  }
  if (actions.navigate_down && selected_index + 1 < static_cast<NSInteger>(all_paths.count)) {
    selected_index++;
    handled = YES;
  }
  if (actions.page_prev && selected_index > 0) {
    NSInteger step = std::max<NSInteger>(1, table_view.indexPathsForVisibleRows.count - 1);
    selected_index = std::max<NSInteger>(0, selected_index - step);
    handled = YES;
  }
  if (actions.page_next && selected_index + 1 < static_cast<NSInteger>(all_paths.count)) {
    NSInteger step = std::max<NSInteger>(1, table_view.indexPathsForVisibleRows.count - 1);
    selected_index = std::min<NSInteger>(static_cast<NSInteger>(all_paths.count - 1),
                                         selected_index + step);
    handled = YES;
  }
  if (actions.section_prev && selected.section > 0) {
    for (NSInteger target_section = selected.section - 1; target_section >= 0; --target_section) {
      NSInteger rows = [table_view numberOfRowsInSection:target_section];
      if (rows > 0) {
        selected = [NSIndexPath indexPathForRow:0 inSection:target_section];
        handled = YES;
        break;
      }
    }
  } else if (actions.section_next && selected.section + 1 < sections) {
    for (NSInteger target_section = selected.section + 1; target_section < sections;
         ++target_section) {
      NSInteger rows = [table_view numberOfRowsInSection:target_section];
      if (rows > 0) {
        selected = [NSIndexPath indexPathForRow:0 inSection:target_section];
        handled = YES;
        break;
      }
    }
  } else {
    selected = all_paths[selected_index];
  }

  if (handled && selected) {
    [table_view selectRowAtIndexPath:selected
                            animated:YES
                      scrollPosition:UITableViewScrollPositionMiddle];
  }

  if (actions.accept && selected) {
    UITableViewCell* cell = [table_view cellForRowAtIndexPath:selected];
    if ([cell.accessoryView isKindOfClass:[UISwitch class]]) {
      UISwitch* toggle = (UISwitch*)cell.accessoryView;
      [toggle setOn:!toggle.isOn animated:YES];
      [toggle sendActionsForControlEvents:UIControlEventValueChanged];
    } else {
      id<UITableViewDelegate> delegate = table_view.delegate;
      if ([delegate respondsToSelector:@selector(tableView:didSelectRowAtIndexPath:)]) {
        [delegate tableView:table_view didSelectRowAtIndexPath:selected];
      }
    }
    handled = YES;
  }

  if (actions.back) {
    UINavigationController* nav = table_controller.navigationController;
    if (nav && nav.viewControllers.count > 1) {
      [nav popViewControllerAnimated:YES];
    } else {
      [table_controller dismissViewControllerAnimated:YES completion:nil];
    }
    handled = YES;
  }

  return handled;
}

- (BOOL)handlePresentedControllerActions:(const xe::ui::apple::ControllerActionSet&)actions {
  UIViewController* presented = self.presentedViewController;
  if (!presented) {
    return NO;
  }

  if ([presented isKindOfClass:[UIAlertController class]]) {
    if (actions.back) {
      [presented dismissViewControllerAnimated:YES completion:nil];
      return YES;
    }
    return NO;
  }

  if ([presented isKindOfClass:[UINavigationController class]]) {
    UINavigationController* nav = (UINavigationController*)presented;
    UIViewController* top = nav.topViewController;
    if ([top isKindOfClass:[XeniaLogViewController class]] &&
        [(XeniaLogViewController*)top handleControllerActions:actions]) {
      return YES;
    }
    if ([top isKindOfClass:[UITableViewController class]] &&
        [self handleControllerActionsForTableController:(UITableViewController*)top
                                                actions:actions]) {
      return YES;
    }
    if (actions.back) {
      if (nav.viewControllers.count > 1) {
        [nav popViewControllerAnimated:YES];
      } else {
        [nav dismissViewControllerAnimated:YES completion:nil];
      }
      return YES;
    }
    return NO;
  }

  if (actions.back) {
    [presented dismissViewControllerAnimated:YES completion:nil];
    return YES;
  }
  return NO;
}

- (BOOL)handleLauncherControllerActions:(const xe::ui::apple::ControllerActionSet&)actions {
  if (self.launcherOverlay.hidden) {
    return NO;
  }

  [self rebuildLauncherFocusGraph];
  IOSFocusNodeId current_focus = launcher_focus_graph_.current();
  NSInteger game_count = static_cast<NSInteger>(discovered_games_.size());
  BOOL handled = NO;
  BOOL focus_changed = NO;

  auto move_focus = [&](xe::ui::apple::NavigationDirection direction) {
    IOSFocusNodeId previous = launcher_focus_graph_.current();
    IOSFocusNodeId next = launcher_focus_graph_.Move(direction);
    if (next != previous) {
      focus_changed = YES;
    }
  };

  if (actions.section_prev) {
    IOSFocusNodeId target = current_focus == kLauncherFocusLibrary ? kLauncherFocusImport
                                                                   : kLauncherFocusSettings;
    if (launcher_focus_graph_.SetCurrent(target)) {
      focus_changed = YES;
    }
    handled = YES;
  }
  if (actions.section_next && game_count > 0) {
    if (launcher_focus_graph_.SetCurrent(kLauncherFocusLibrary)) {
      focus_changed = YES;
    }
    handled = YES;
  }

  current_focus = launcher_focus_graph_.current();
  if (current_focus == kLauncherFocusLibrary && game_count > 0) {
    NSInteger columns = [self launcherGridColumnCount];
    NSInteger next_index = focused_game_index_ < 0 ? 0 : focused_game_index_;

    if (actions.navigate_left) {
      if (next_index % columns == 0) {
        move_focus(xe::ui::apple::NavigationDirection::kLeft);
      } else if (next_index > 0) {
        next_index--;
      }
      handled = YES;
    }
    if (actions.navigate_right) {
      if (next_index + 1 < game_count) {
        next_index++;
      }
      handled = YES;
    }
    if (actions.navigate_up) {
      if (next_index - columns >= 0) {
        next_index -= columns;
      } else {
        move_focus(xe::ui::apple::NavigationDirection::kUp);
      }
      handled = YES;
    }
    if (actions.navigate_down) {
      if (next_index + columns < game_count) {
        next_index += columns;
      }
      handled = YES;
    }
    if (actions.page_prev) {
      NSInteger page_step = [self launcherPageStep];
      next_index = std::max<NSInteger>(0, next_index - page_step);
      handled = YES;
    }
    if (actions.page_next) {
      NSInteger page_step = [self launcherPageStep];
      next_index = std::min<NSInteger>(game_count - 1, next_index + page_step);
      handled = YES;
    }

    if (launcher_focus_graph_.current() == kLauncherFocusLibrary &&
        next_index != focused_game_index_) {
      [self setFocusedGameIndex:next_index scroll:YES];
      handled = YES;
    }
  } else {
    if (actions.navigate_up) {
      move_focus(xe::ui::apple::NavigationDirection::kUp);
      handled = YES;
    }
    if (actions.navigate_down) {
      move_focus(xe::ui::apple::NavigationDirection::kDown);
      handled = YES;
    }
    if (actions.navigate_left) {
      move_focus(xe::ui::apple::NavigationDirection::kLeft);
      handled = YES;
    }
    if (actions.navigate_right) {
      move_focus(xe::ui::apple::NavigationDirection::kRight);
      handled = YES;
    }
  }

  if (actions.context) {
    if (launcher_focus_graph_.current() == kLauncherFocusLibrary && focused_game_index_ >= 0 &&
        focused_game_index_ < game_count) {
      [self presentManageContentSheetForIndex:static_cast<size_t>(focused_game_index_)];
    } else {
      [self openProfileTapped:self.profileButton];
    }
    handled = YES;
  }
  if (actions.quick_action) {
    [self openGameTapped:self.openGameButton];
    handled = YES;
  }
  if (actions.guide) {
    [self openSettingsTapped:self.settingsButton];
    handled = YES;
  }

  if (actions.accept) {
    switch (launcher_focus_graph_.current()) {
      case kLauncherFocusSettings:
        [self openSettingsTapped:self.settingsButton];
        handled = YES;
        break;
      case kLauncherFocusProfile:
        [self openProfileTapped:self.profileButton];
        handled = YES;
        break;
      case kLauncherFocusImport:
        [self openGameTapped:self.openGameButton];
        handled = YES;
        break;
      case kLauncherFocusLibrary:
        if (focused_game_index_ >= 0 && focused_game_index_ < game_count) {
          const IOSDiscoveredGame& game = discovered_games_[focused_game_index_];
          [self launchGameAtPath:game.path displayName:ToNSString(game.title)];
          handled = YES;
        }
        break;
      default:
        break;
    }
  }

  if (focus_changed || handled) {
    [self applyLauncherFocusVisuals];
  }
  return handled;
}

- (BOOL)handleInGameControllerActions:(const xe::ui::apple::ControllerActionSet&)actions {
  if (self.launcherOverlay.hidden == NO || !self.gameRunning) {
    return NO;
  }

  if (actions.guide) {
    if (self.inGameMenuOverlay.hidden) {
      self.inGameMenuOverlay.alpha = 0.0;
      self.inGameMenuOverlay.hidden = NO;
      [self rebuildInGameFocusGraph];
      in_game_focus_graph_.SetCurrent(kInGameFocusResume);
      [self applyInGameMenuFocusVisuals];
      [UIView animateWithDuration:0.18
                       animations:^{
                         self.inGameMenuOverlay.alpha = 1.0;
                       }];
    } else {
      [self hideInGameMenuOverlay];
    }
    return YES;
  }

  if (self.inGameMenuOverlay.hidden) {
    return NO;
  }

  if (actions.back) {
    [self hideInGameMenuOverlay];
    return YES;
  }

  [self rebuildInGameFocusGraph];
  if (in_game_focus_graph_.current() == xe::ui::apple::kInvalidFocusNodeId) {
    in_game_focus_graph_.SetCurrent(kInGameFocusResume);
  }

  BOOL handled = NO;
  BOOL focus_changed = NO;
  auto move_focus = [&](xe::ui::apple::NavigationDirection direction) {
    IOSFocusNodeId previous = in_game_focus_graph_.current();
    IOSFocusNodeId next = in_game_focus_graph_.Move(direction);
    if (next != previous) {
      focus_changed = YES;
    }
  };

  if (actions.navigate_up) {
    move_focus(xe::ui::apple::NavigationDirection::kUp);
    handled = YES;
  }
  if (actions.navigate_down) {
    move_focus(xe::ui::apple::NavigationDirection::kDown);
    handled = YES;
  }
  if (actions.navigate_left) {
    move_focus(xe::ui::apple::NavigationDirection::kUp);
    handled = YES;
  }
  if (actions.navigate_right) {
    move_focus(xe::ui::apple::NavigationDirection::kDown);
    handled = YES;
  }

  if (actions.section_prev && in_game_focus_graph_.SetCurrent(kInGameFocusResume)) {
    focus_changed = YES;
    handled = YES;
  }
  if (actions.section_next && in_game_focus_graph_.SetCurrent(kInGameFocusExit)) {
    focus_changed = YES;
    handled = YES;
  }

  if (actions.context) {
    [self inGameSettingsTapped:self.inGameSettingsButton];
    handled = YES;
  }
  if (actions.quick_action) {
    [self inGameLiveLogTapped:self.inGameLiveLogButton];
    handled = YES;
  }

  if (actions.accept) {
    switch (in_game_focus_graph_.current()) {
      case kInGameFocusResume:
        [self.inGameResumeButton sendActionsForControlEvents:UIControlEventTouchUpInside];
        break;
      case kInGameFocusSettings:
        [self.inGameSettingsButton sendActionsForControlEvents:UIControlEventTouchUpInside];
        break;
      case kInGameFocusLog:
        [self.inGameLiveLogButton sendActionsForControlEvents:UIControlEventTouchUpInside];
        break;
      case kInGameFocusExit:
        [self.inGameExitButton sendActionsForControlEvents:UIControlEventTouchUpInside];
        break;
      default:
        break;
    }
    handled = YES;
  }

  if (focus_changed || handled) {
    [self applyInGameMenuFocusVisuals];
  }
  return handled;
}

- (void)pollControllerNavigation:(NSTimer* __unused)timer {
  const bool navigation_enabled = cvars::ui_controller_navigation;
  if (!navigation_enabled) {
    if (controller_navigation_was_enabled_) {
      controller_navigation_was_enabled_ = NO;
      controller_navigation_mapper_.Reset();
      launcher_focus_graph_.Clear();
      in_game_focus_graph_.Clear();
      [self applyLauncherFocusVisuals];
      [self applyInGameMenuFocusVisuals];
    }
    return;
  }

  if (!controller_navigation_was_enabled_) {
    controller_navigation_was_enabled_ = YES;
    [self rebuildLauncherFocusGraph];
    [self applyLauncherFocusVisuals];
  }

  xe::hid::X_INPUT_STATE state = {};
  bool has_state = false;
  if (self.appContext) {
    for (uint32_t user_index = 0; user_index < xe::XUserMaxUserCount; ++user_index) {
      if (self.appContext->GetControllerState(user_index, &state)) {
        has_state = true;
        break;
      }
    }
  }
  if (!has_state) {
    has_state = [self readNativeControllerState:&state];
  }
  if (!has_state) {
    controller_navigation_mapper_.Reset();
    return;
  }

  xe::ui::apple::ControllerActionSet actions =
      controller_navigation_mapper_.Update(state, GetNowMs());
  if (!actions.Any()) {
    return;
  }

  if ([self handlePresentedControllerActions:actions]) {
    return;
  }
  if ([self handleLauncherControllerActions:actions]) {
    return;
  }
  [self handleInGameControllerActions:actions];
}

- (BOOL)readNativeControllerState:(xe::hid::X_INPUT_STATE*)out_state {
  if (!out_state) {
    return NO;
  }

  NSArray<GCController*>* controllers = [GCController controllers];
  for (GCController* controller in controllers) {
    GCExtendedGamepad* gamepad = controller.extendedGamepad;
    if (!gamepad) {
      continue;
    }

    uint16_t buttons = 0;
    auto set_button = [&buttons](BOOL pressed, uint16_t mask) {
      if (pressed) {
        buttons |= mask;
      }
    };

    set_button(gamepad.dpad.up.pressed, xe::hid::X_INPUT_GAMEPAD_DPAD_UP);
    set_button(gamepad.dpad.down.pressed, xe::hid::X_INPUT_GAMEPAD_DPAD_DOWN);
    set_button(gamepad.dpad.left.pressed, xe::hid::X_INPUT_GAMEPAD_DPAD_LEFT);
    set_button(gamepad.dpad.right.pressed, xe::hid::X_INPUT_GAMEPAD_DPAD_RIGHT);
    set_button(gamepad.buttonA.pressed, xe::hid::X_INPUT_GAMEPAD_A);
    set_button(gamepad.buttonB.pressed, xe::hid::X_INPUT_GAMEPAD_B);
    set_button(gamepad.buttonX.pressed, xe::hid::X_INPUT_GAMEPAD_X);
    set_button(gamepad.buttonY.pressed, xe::hid::X_INPUT_GAMEPAD_Y);
    set_button(gamepad.leftShoulder.pressed, xe::hid::X_INPUT_GAMEPAD_LEFT_SHOULDER);
    set_button(gamepad.rightShoulder.pressed, xe::hid::X_INPUT_GAMEPAD_RIGHT_SHOULDER);

    if (@available(iOS 13.0, tvOS 13.0, macCatalyst 13.0, *)) {
      set_button(gamepad.buttonMenu.pressed, xe::hid::X_INPUT_GAMEPAD_START);
    }
    if (@available(iOS 14.0, tvOS 14.0, macCatalyst 14.0, *)) {
      set_button(gamepad.buttonOptions.pressed, xe::hid::X_INPUT_GAMEPAD_BACK);
    }
    if (@available(iOS 12.1, tvOS 12.1, macCatalyst 13.1, *)) {
      set_button(gamepad.leftThumbstickButton.pressed, xe::hid::X_INPUT_GAMEPAD_LEFT_THUMB);
      set_button(gamepad.rightThumbstickButton.pressed, xe::hid::X_INPUT_GAMEPAD_RIGHT_THUMB);
    }

    out_state->packet_number = ++native_controller_packet_number_;
    out_state->gamepad.buttons = buttons;
    out_state->gamepad.left_trigger = ToTriggerAxis(gamepad.leftTrigger.value);
    out_state->gamepad.right_trigger = ToTriggerAxis(gamepad.rightTrigger.value);
    out_state->gamepad.thumb_lx = ToThumbAxis(gamepad.leftThumbstick.xAxis.value);
    out_state->gamepad.thumb_ly = ToThumbAxis(gamepad.leftThumbstick.yAxis.value);
    out_state->gamepad.thumb_rx = ToThumbAxis(gamepad.rightThumbstick.xAxis.value);
    out_state->gamepad.thumb_ry = ToThumbAxis(gamepad.rightThumbstick.yAxis.value);
    return YES;
  }
  return NO;
}

// ---------------------------------------------------------------------------
// JIT polling -- checks every 0.5s until JIT is available.
// ---------------------------------------------------------------------------
- (void)startJITPoll {
  // Check immediately first.
  if (xe_check_jit_available()) {
    [self onJITAcquired];
    return;
  }

  XELOGI("iOS: JIT not yet available, polling...");
  self.jitPollTimer = [NSTimer scheduledTimerWithTimeInterval:0.5
                                                       target:self
                                                     selector:@selector(pollJIT:)
                                                     userInfo:nil
                                                      repeats:YES];
}

- (void)pollJIT:(NSTimer*)timer {
  if (xe_check_jit_available()) {
    [timer invalidate];
    self.jitPollTimer = nil;
    [self onJITAcquired];
  }
}

- (void)onJITAcquired {
  self.jitAcquired = YES;
  XELOGI("iOS: JIT acquired!");
  [self updateJITStatusIndicator];
  [self updateJITAvailabilityUI];

  if (!pending_external_launch_path_.empty()) {
    std::filesystem::path queued_path = pending_external_launch_path_;
    pending_external_launch_path_.clear();
    XELOGI("iOS: Launching queued external request: {}",
           queued_path.string());
    [self launchGameAtPath:queued_path
               displayName:ToNSString(queued_path.filename().string())];
  }
}

// ---------------------------------------------------------------------------
// Launcher overlay — content-first design matching xenios-website aesthetic.
// ---------------------------------------------------------------------------
- (void)setupLauncherOverlay {
  self.launcherOverlay = [[UIView alloc] initWithFrame:self.view.bounds];
  self.launcherOverlay.autoresizingMask =
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  self.launcherOverlay.backgroundColor = [XeniaTheme bgPrimary];
  [self.view addSubview:self.launcherOverlay];
  UILayoutGuide* safe = self.launcherOverlay.safeAreaLayoutGuide;
  CGFloat hPad = 16.0;

  // ── Nav bar: XeniOS (left) · gear + profile (right) ────────────────────

  self.titleLabel = [[UILabel alloc] init];
  self.titleLabel.text = @"XeniOS";
  self.titleLabel.textColor = [XeniaTheme textPrimary];
  self.titleLabel.font = [UIFont systemFontOfSize:22 weight:UIFontWeightBold];
  self.titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
  [self.launcherOverlay addSubview:self.titleLabel];

  UIButtonConfiguration* settingsCfg = [UIButtonConfiguration plainButtonConfiguration];
  settingsCfg.image =
      [UIImage systemImageNamed:@"gearshape"
              withConfiguration:[UIImageSymbolConfiguration
                                    configurationWithPointSize:20
                                                       weight:UIImageSymbolWeightRegular]];
  settingsCfg.baseForegroundColor = [XeniaTheme textMuted];
  settingsCfg.contentInsets = NSDirectionalEdgeInsetsMake(8, 8, 8, 8);
  self.settingsButton = [UIButton buttonWithConfiguration:settingsCfg primaryAction:nil];
  self.settingsButton.translatesAutoresizingMaskIntoConstraints = NO;
  [self.settingsButton addTarget:self
                          action:@selector(openSettingsTapped:)
                forControlEvents:UIControlEventTouchUpInside];
  [self.launcherOverlay addSubview:self.settingsButton];

  UIButtonConfiguration* profileCfg = [UIButtonConfiguration plainButtonConfiguration];
  profileCfg.image =
      [UIImage systemImageNamed:@"person.circle"
              withConfiguration:[UIImageSymbolConfiguration
                                    configurationWithPointSize:20
                                                       weight:UIImageSymbolWeightRegular]];
  profileCfg.baseForegroundColor = [XeniaTheme textMuted];
  profileCfg.contentInsets = NSDirectionalEdgeInsetsMake(8, 8, 8, 8);
  self.profileButton = [UIButton buttonWithConfiguration:profileCfg primaryAction:nil];
  self.profileButton.translatesAutoresizingMaskIntoConstraints = NO;
  [self.profileButton addTarget:self
                         action:@selector(openProfileTapped:)
               forControlEvents:UIControlEventTouchUpInside];
  [self.launcherOverlay addSubview:self.profileButton];

  // Thin separator below the nav bar.
  UIView* navSep = [[UIView alloc] init];
  navSep.translatesAutoresizingMaskIntoConstraints = NO;
  navSep.backgroundColor = [XeniaTheme border];
  [self.launcherOverlay addSubview:navSep];

  // ── JIT warning banner (single source of JIT info; collapses via stack) ─
  //
  // Contains the status dot + label + guidance text in one compact row.
  // When JIT is acquired, updateJITAvailabilityUI sets .hidden = YES
  // and the UIStackView collapses it automatically.

  self.jitWarningCard = [[UIView alloc] init];
  self.jitWarningCard.translatesAutoresizingMaskIntoConstraints = NO;
  self.jitWarningCard.backgroundColor = [XeniaTheme bgSurface];
  self.jitWarningCard.layer.cornerRadius = XeniaRadiusMd;
  self.jitWarningCard.layer.borderWidth = 0.5;
  self.jitWarningCard.layer.borderColor = [XeniaTheme border].CGColor;

  self.jitStatusDot = [[UIView alloc] init];
  self.jitStatusDot.translatesAutoresizingMaskIntoConstraints = NO;
  self.jitStatusDot.backgroundColor = [XeniaTheme statusWarning];
  self.jitStatusDot.layer.cornerRadius = 4;
  [self.jitWarningCard addSubview:self.jitStatusDot];

  self.jitStatusLabel = [[UILabel alloc] init];
  self.jitStatusLabel.translatesAutoresizingMaskIntoConstraints = NO;
  self.jitStatusLabel.text = @"JIT Not Detected";
  self.jitStatusLabel.textColor = [XeniaTheme textPrimary];
  self.jitStatusLabel.font = [UIFont systemFontOfSize:15 weight:UIFontWeightMedium];
  [self.jitWarningCard addSubview:self.jitStatusLabel];

  UILabel* jitGuide = [[UILabel alloc] init];
  jitGuide.translatesAutoresizingMaskIntoConstraints = NO;
  jitGuide.text = xe_jit_not_detected_guidance_message();
  jitGuide.textColor = [XeniaTheme textSecondary];
  jitGuide.font = [UIFont systemFontOfSize:14 weight:UIFontWeightRegular];
  jitGuide.numberOfLines = 0;
  [self.jitWarningCard addSubview:jitGuide];

  // ── Library header: "Library" (left) + "+" (right) ─────────────────────

  UIView* libraryRow = [[UIView alloc] init];
  libraryRow.translatesAutoresizingMaskIntoConstraints = NO;

  UILabel* libraryLabel = [[UILabel alloc] init];
  libraryLabel.translatesAutoresizingMaskIntoConstraints = NO;
  libraryLabel.text = @"Library";
  libraryLabel.textColor = [XeniaTheme textPrimary];
  libraryLabel.font = [UIFont systemFontOfSize:20 weight:UIFontWeightSemibold];
  [libraryRow addSubview:libraryLabel];

  UIButtonConfiguration* importCfg = [UIButtonConfiguration plainButtonConfiguration];
  importCfg.image =
      [UIImage systemImageNamed:@"plus"
              withConfiguration:[UIImageSymbolConfiguration
                                    configurationWithPointSize:20
                                                       weight:UIImageSymbolWeightMedium]];
  importCfg.baseForegroundColor = [XeniaTheme accent];
  importCfg.contentInsets = NSDirectionalEdgeInsetsMake(6, 6, 6, 6);
  self.openGameButton = [UIButton buttonWithConfiguration:importCfg primaryAction:nil];
  self.openGameButton.translatesAutoresizingMaskIntoConstraints = NO;
  [self.openGameButton addTarget:self
                          action:@selector(openGameTapped:)
                forControlEvents:UIControlEventTouchUpInside];
  [libraryRow addSubview:self.openGameButton];

  // ── Collapsible stack: JIT banner → library header ─────────────────────

  UIStackView* headerStack = [[UIStackView alloc]
      initWithArrangedSubviews:@[ self.jitWarningCard, libraryRow ]];
  headerStack.axis = UILayoutConstraintAxisVertical;
  headerStack.spacing = 12;
  headerStack.translatesAutoresizingMaskIntoConstraints = NO;
  [self.launcherOverlay addSubview:headerStack];

  // ── Games grid ─────────────────────────────────────────────────────────

  UICollectionViewFlowLayout* gridLayout = [[UICollectionViewFlowLayout alloc] init];
  gridLayout.minimumInteritemSpacing = 16;
  gridLayout.minimumLineSpacing = 16;
  self.importedGamesCollectionView =
      [[UICollectionView alloc] initWithFrame:CGRectZero collectionViewLayout:gridLayout];
  self.importedGamesCollectionView.translatesAutoresizingMaskIntoConstraints = NO;
  self.importedGamesCollectionView.dataSource = self;
  self.importedGamesCollectionView.delegate = self;
  self.importedGamesCollectionView.backgroundColor = [UIColor clearColor];
  self.importedGamesCollectionView.alwaysBounceVertical = YES;
  [self.importedGamesCollectionView registerClass:[XeniaGameTileCell class]
                       forCellWithReuseIdentifier:@"ImportedGameCell"];
  [self.launcherOverlay addSubview:self.importedGamesCollectionView];

  // Empty-state label.
  UIView* emptyBg = [[UIView alloc] initWithFrame:CGRectZero];
  self.importedGamesEmptyLabel = [[UILabel alloc] init];
  self.importedGamesEmptyLabel.translatesAutoresizingMaskIntoConstraints = NO;
  self.importedGamesEmptyLabel.text =
      @"No games yet.\nTransfer ISO or GOD files to the\nDocuments folder, or tap +.";
  self.importedGamesEmptyLabel.textColor = [XeniaTheme textMuted];
  self.importedGamesEmptyLabel.font = [UIFont systemFontOfSize:15 weight:UIFontWeightRegular];
  self.importedGamesEmptyLabel.textAlignment = NSTextAlignmentCenter;
  self.importedGamesEmptyLabel.numberOfLines = 0;
  [emptyBg addSubview:self.importedGamesEmptyLabel];
  [NSLayoutConstraint activateConstraints:@[
    [self.importedGamesEmptyLabel.centerXAnchor constraintEqualToAnchor:emptyBg.centerXAnchor],
    [self.importedGamesEmptyLabel.centerYAnchor constraintEqualToAnchor:emptyBg.centerYAnchor],
    [self.importedGamesEmptyLabel.leadingAnchor
        constraintGreaterThanOrEqualToAnchor:emptyBg.leadingAnchor
                                    constant:32],
    [self.importedGamesEmptyLabel.trailingAnchor
        constraintLessThanOrEqualToAnchor:emptyBg.trailingAnchor
                                 constant:-32],
  ]];
  self.importedGamesCollectionView.backgroundView = emptyBg;

  // ── Status label (tiny, at bottom — invisible when text is empty) ──────

  self.statusLabel = [[UILabel alloc] init];
  self.statusLabel.text = @"";
  self.statusLabel.textColor = [XeniaTheme textMuted];
  self.statusLabel.font = [UIFont systemFontOfSize:13 weight:UIFontWeightRegular];
  self.statusLabel.textAlignment = NSTextAlignmentCenter;
  self.statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
  [self.launcherOverlay addSubview:self.statusLabel];

  // Allocated but off-screen — existing code can set .text without crashing.
  self.signedInProfileLabel = [[UILabel alloc] init];

  // ── Layout ─────────────────────────────────────────────────────────────

  [NSLayoutConstraint activateConstraints:@[
    // Nav bar.
    [self.titleLabel.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:hPad],
    [self.titleLabel.topAnchor constraintEqualToAnchor:safe.topAnchor constant:8],
    [self.profileButton.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-8],
    [self.profileButton.centerYAnchor constraintEqualToAnchor:self.titleLabel.centerYAnchor],
    [self.settingsButton.trailingAnchor
        constraintEqualToAnchor:self.profileButton.leadingAnchor],
    [self.settingsButton.centerYAnchor constraintEqualToAnchor:self.titleLabel.centerYAnchor],

    // Nav separator.
    [navSep.topAnchor constraintEqualToAnchor:self.titleLabel.bottomAnchor constant:8],
    [navSep.leadingAnchor constraintEqualToAnchor:self.launcherOverlay.leadingAnchor],
    [navSep.trailingAnchor constraintEqualToAnchor:self.launcherOverlay.trailingAnchor],
    [navSep.heightAnchor constraintEqualToConstant:0.5],

    // Header stack (JIT banner + library row) below separator.
    [headerStack.topAnchor constraintEqualToAnchor:navSep.bottomAnchor constant:12],
    [headerStack.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:hPad],
    [headerStack.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-hPad],

    // JIT banner internals.
    [self.jitStatusDot.leadingAnchor constraintEqualToAnchor:self.jitWarningCard.leadingAnchor
                                                    constant:14],
    [self.jitStatusDot.topAnchor constraintEqualToAnchor:self.jitWarningCard.topAnchor
                                                constant:16],
    [self.jitStatusDot.widthAnchor constraintEqualToConstant:8],
    [self.jitStatusDot.heightAnchor constraintEqualToConstant:8],
    [self.jitStatusLabel.leadingAnchor constraintEqualToAnchor:self.jitStatusDot.trailingAnchor
                                                      constant:8],
    [self.jitStatusLabel.trailingAnchor
        constraintEqualToAnchor:self.jitWarningCard.trailingAnchor
                       constant:-14],
    [self.jitStatusLabel.centerYAnchor constraintEqualToAnchor:self.jitStatusDot.centerYAnchor],
    [jitGuide.leadingAnchor constraintEqualToAnchor:self.jitStatusLabel.leadingAnchor],
    [jitGuide.trailingAnchor constraintEqualToAnchor:self.jitStatusLabel.trailingAnchor],
    [jitGuide.topAnchor constraintEqualToAnchor:self.jitStatusLabel.bottomAnchor constant:2],
    [jitGuide.bottomAnchor constraintEqualToAnchor:self.jitWarningCard.bottomAnchor
                                           constant:-14],

    // Library row internals.
    [libraryLabel.leadingAnchor constraintEqualToAnchor:libraryRow.leadingAnchor],
    [libraryLabel.centerYAnchor constraintEqualToAnchor:libraryRow.centerYAnchor],
    [self.openGameButton.trailingAnchor constraintEqualToAnchor:libraryRow.trailingAnchor],
    [self.openGameButton.centerYAnchor constraintEqualToAnchor:libraryRow.centerYAnchor],
    [libraryRow.heightAnchor constraintEqualToConstant:34],

    // Games grid.
    [self.importedGamesCollectionView.topAnchor constraintEqualToAnchor:headerStack.bottomAnchor
                                                               constant:8],
    [self.importedGamesCollectionView.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor
                                                                   constant:hPad],
    [self.importedGamesCollectionView.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor
                                                                    constant:-hPad],
    [self.importedGamesCollectionView.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor],

    // Status label.
    [self.statusLabel.centerXAnchor constraintEqualToAnchor:safe.centerXAnchor],
    [self.statusLabel.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-6],
  ]];
}

- (void)setupInGameMenuOverlay {
  self.inGameMenuOverlay = [[UIView alloc] initWithFrame:self.view.bounds];
  self.inGameMenuOverlay.autoresizingMask =
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  self.inGameMenuOverlay.backgroundColor = [XeniaTheme overlayLight];
  self.inGameMenuOverlay.hidden = YES;
  [self.view addSubview:self.inGameMenuOverlay];

  UIView* panel = [[UIView alloc] init];
  panel.translatesAutoresizingMaskIntoConstraints = NO;
  panel.backgroundColor = [XeniaTheme bgSurface];
  panel.layer.cornerRadius = XeniaRadiusXl;
  panel.layer.borderWidth = 1.0;
  panel.layer.borderColor = [XeniaTheme border].CGColor;
  [self.inGameMenuOverlay addSubview:panel];

  UILabel* title = [[UILabel alloc] init];
  title.translatesAutoresizingMaskIntoConstraints = NO;
  title.text = @"In-Game Menu";
  title.textColor = [XeniaTheme textPrimary];
  title.font = [UIFont systemFontOfSize:22 weight:UIFontWeightSemibold];
  title.textAlignment = NSTextAlignmentCenter;
  [panel addSubview:title];

  UILabel* subtitle = [[UILabel alloc] init];
  subtitle.translatesAutoresizingMaskIntoConstraints = NO;
  subtitle.text = @"Tap anywhere to close";
  subtitle.textColor = [XeniaTheme textMuted];
  subtitle.font = [UIFont systemFontOfSize:15 weight:UIFontWeightRegular];
  subtitle.textAlignment = NSTextAlignmentCenter;
  [panel addSubview:subtitle];

  UIButtonConfiguration* resume_config = [UIButtonConfiguration filledButtonConfiguration];
  resume_config.title = @"Resume";
  resume_config.baseBackgroundColor = [XeniaTheme accent];
  resume_config.baseForegroundColor = [XeniaTheme accentFg];
  resume_config.cornerStyle = UIButtonConfigurationCornerStyleLarge;
  resume_config.contentInsets = NSDirectionalEdgeInsetsMake(12, 18, 12, 18);
  self.inGameResumeButton = [UIButton buttonWithConfiguration:resume_config primaryAction:nil];
  self.inGameResumeButton.translatesAutoresizingMaskIntoConstraints = NO;
  [self.inGameResumeButton addTarget:self
                              action:@selector(resumeGameTapped:)
                    forControlEvents:UIControlEventTouchUpInside];
  [panel addSubview:self.inGameResumeButton];

  UIButtonConfiguration* settings_config = [UIButtonConfiguration tintedButtonConfiguration];
  settings_config.title = @"Settings";
  settings_config.image = [UIImage systemImageNamed:@"slider.horizontal.3"];
  settings_config.imagePadding = 6;
  settings_config.baseForegroundColor = [XeniaTheme textPrimary];
  settings_config.baseBackgroundColor = [XeniaTheme bgSurface2];
  settings_config.cornerStyle = UIButtonConfigurationCornerStyleLarge;
  settings_config.contentInsets = NSDirectionalEdgeInsetsMake(10, 16, 10, 16);
  self.inGameSettingsButton = [UIButton buttonWithConfiguration:settings_config primaryAction:nil];
  self.inGameSettingsButton.translatesAutoresizingMaskIntoConstraints = NO;
  [self.inGameSettingsButton addTarget:self
                                action:@selector(inGameSettingsTapped:)
                      forControlEvents:UIControlEventTouchUpInside];
  [panel addSubview:self.inGameSettingsButton];

  UIButtonConfiguration* live_log_config = [UIButtonConfiguration tintedButtonConfiguration];
  live_log_config.title = @"Live Log";
  live_log_config.image = [UIImage systemImageNamed:@"doc.text"];
  live_log_config.imagePadding = 6;
  live_log_config.baseForegroundColor = [XeniaTheme textPrimary];
  live_log_config.baseBackgroundColor = [XeniaTheme bgSurface2];
  live_log_config.cornerStyle = UIButtonConfigurationCornerStyleLarge;
  live_log_config.contentInsets = NSDirectionalEdgeInsetsMake(10, 16, 10, 16);
  self.inGameLiveLogButton = [UIButton buttonWithConfiguration:live_log_config primaryAction:nil];
  self.inGameLiveLogButton.translatesAutoresizingMaskIntoConstraints = NO;
  [self.inGameLiveLogButton addTarget:self
                               action:@selector(inGameLiveLogTapped:)
                     forControlEvents:UIControlEventTouchUpInside];
  [panel addSubview:self.inGameLiveLogButton];

  UIButtonConfiguration* exit_config = [UIButtonConfiguration tintedButtonConfiguration];
  exit_config.title = @"Exit To Library";
  exit_config.image = [UIImage systemImageNamed:@"rectangle.portrait.and.arrow.right"];
  exit_config.imagePadding = 6;
  exit_config.baseForegroundColor = [XeniaTheme textPrimary];
  exit_config.baseBackgroundColor = [[XeniaTheme statusError] colorWithAlphaComponent:0.25];
  exit_config.cornerStyle = UIButtonConfigurationCornerStyleLarge;
  exit_config.contentInsets = NSDirectionalEdgeInsetsMake(10, 16, 10, 16);
  self.inGameExitButton = [UIButton buttonWithConfiguration:exit_config primaryAction:nil];
  self.inGameExitButton.translatesAutoresizingMaskIntoConstraints = NO;
  [self.inGameExitButton addTarget:self
                            action:@selector(exitGameTapped:)
                  forControlEvents:UIControlEventTouchUpInside];
  [panel addSubview:self.inGameExitButton];

  [NSLayoutConstraint activateConstraints:@[
    [panel.centerXAnchor constraintEqualToAnchor:self.inGameMenuOverlay.centerXAnchor],
    [panel.centerYAnchor constraintEqualToAnchor:self.inGameMenuOverlay.centerYAnchor],
    [panel.leadingAnchor constraintGreaterThanOrEqualToAnchor:self.inGameMenuOverlay.safeAreaLayoutGuide.leadingAnchor
                                                     constant:24],
    [panel.trailingAnchor constraintLessThanOrEqualToAnchor:self.inGameMenuOverlay.safeAreaLayoutGuide.trailingAnchor
                                                   constant:-24],
    [panel.widthAnchor constraintLessThanOrEqualToConstant:420],

    [title.topAnchor constraintEqualToAnchor:panel.topAnchor constant:18],
    [title.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor constant:20],
    [title.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor constant:-20],

    [subtitle.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:4],
    [subtitle.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
    [subtitle.trailingAnchor constraintEqualToAnchor:title.trailingAnchor],

    [self.inGameResumeButton.topAnchor constraintEqualToAnchor:subtitle.bottomAnchor constant:16],
    [self.inGameResumeButton.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor constant:14],
    [self.inGameResumeButton.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor constant:-14],

    [self.inGameSettingsButton.topAnchor constraintEqualToAnchor:self.inGameResumeButton.bottomAnchor
                                                        constant:10],
    [self.inGameSettingsButton.leadingAnchor constraintEqualToAnchor:self.inGameResumeButton.leadingAnchor],
    [self.inGameSettingsButton.trailingAnchor
        constraintEqualToAnchor:self.inGameResumeButton.trailingAnchor],

    [self.inGameLiveLogButton.topAnchor constraintEqualToAnchor:self.inGameSettingsButton.bottomAnchor
                                                       constant:10],
    [self.inGameLiveLogButton.leadingAnchor constraintEqualToAnchor:self.inGameResumeButton.leadingAnchor],
    [self.inGameLiveLogButton.trailingAnchor
        constraintEqualToAnchor:self.inGameResumeButton.trailingAnchor],

    [self.inGameExitButton.topAnchor constraintEqualToAnchor:self.inGameLiveLogButton.bottomAnchor
                                                    constant:10],
    [self.inGameExitButton.leadingAnchor constraintEqualToAnchor:self.inGameResumeButton.leadingAnchor],
    [self.inGameExitButton.trailingAnchor
        constraintEqualToAnchor:self.inGameResumeButton.trailingAnchor],
    [self.inGameExitButton.bottomAnchor constraintEqualToAnchor:panel.bottomAnchor constant:-14],
  ]];
}

- (void)toggleInGameMenuTapped:(UITapGestureRecognizer*)recognizer {
  if (recognizer.state != UIGestureRecognizerStateRecognized) {
    return;
  }
  if (self.launcherOverlay.hidden == NO || !self.gameRunning || self.presentedViewController) {
    return;
  }

  BOOL should_show = self.inGameMenuOverlay.hidden;
  if (should_show) {
    [self rebuildInGameFocusGraph];
    in_game_focus_graph_.SetCurrent(kInGameFocusResume);
    self.inGameMenuOverlay.alpha = 0.0;
    self.inGameMenuOverlay.hidden = NO;
    [self applyInGameMenuFocusVisuals];
    [UIView animateWithDuration:0.18
                     animations:^{
                       self.inGameMenuOverlay.alpha = 1.0;
                     }];
  } else {
    [self hideInGameMenuOverlay];
  }
}

- (void)hideInGameMenuOverlay {
  if (self.inGameMenuOverlay.hidden) {
    return;
  }
  [UIView animateWithDuration:0.15
      animations:^{
        self.inGameMenuOverlay.alpha = 0.0;
      }
      completion:^(__unused BOOL finished) {
        self.inGameMenuOverlay.hidden = YES;
        self.inGameMenuOverlay.alpha = 1.0;
        [self applyInGameMenuFocusVisuals];
      }];
}

- (void)resumeGameTapped:(UIButton*)sender {
  [self hideInGameMenuOverlay];
}

- (void)inGameSettingsTapped:(UIButton*)sender {
  [self hideInGameMenuOverlay];
  [self openSettingsTapped:nil];
}

- (void)inGameLiveLogTapped:(UIButton*)sender {
  [self hideInGameMenuOverlay];
  XeniaLogViewController* log_vc = [[XeniaLogViewController alloc] init];
  XeniaLandscapeNavigationController* nav =
      [[XeniaLandscapeNavigationController alloc] initWithRootViewController:log_vc];
  if (UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad) {
    nav.modalPresentationStyle = UIModalPresentationFormSheet;
    if (@available(iOS 15.0, *)) {
      UISheetPresentationController* sheet = nav.sheetPresentationController;
      sheet.detents = @[ [UISheetPresentationControllerDetent mediumDetent],
                         [UISheetPresentationControllerDetent largeDetent] ];
      sheet.prefersGrabberVisible = YES;
    }
  } else {
    nav.modalPresentationStyle = UIModalPresentationFullScreen;
  }
  [self presentViewController:nav animated:YES completion:nil];
}

- (void)exitGameTapped:(UIButton*)sender {
  [self hideInGameMenuOverlay];
  if (self.gameStopInProgress) {
    self.statusLabel.text = @"Stopping game... Please wait.";
    return;
  }
  if (!self.appContext) {
    self.statusLabel.text = @"No active game to stop.";
    return;
  }

  self.gameStopInProgress = YES;
  self.gameRunning = NO;
  self.launcherOverlay.hidden = NO;
  self.launcherOverlay.alpha = 1.0;
  xe_request_portrait_orientation(self);
  self.statusLabel.text = @"Stopping game...";

  xe::ui::IOSWindowedAppContext* app_context = self.appContext;
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    BOOL requested_stop = app_context->TerminateCurrentGame() ? YES : NO;
    if (requested_stop) {
      return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      self.gameStopInProgress = NO;
      self.statusLabel.text = @"No active game to stop.";
    });
  });
}

- (void)refreshSignedInProfileUI {
  if (!self.appContext) {
    self.profileButton.enabled = NO;
    self.profileButton.alpha = 0.5;
    self.signedInProfileLabel.text = @"Profile system unavailable";
    return;
  }

  self.profileButton.enabled = YES;
  self.profileButton.alpha = 1.0;

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
      addAction:
          [UIAlertAction
              actionWithTitle:@"Create"
                        style:UIAlertActionStyleDefault
                      handler:^(__unused UIAlertAction* action) {
                        if (!self.appContext) {
                          return;
                        }
                        UITextField* text_field = create_alert.textFields.firstObject;
                        NSString* raw_text = text_field.text ?: @"";
                        NSString* trimmed = [raw_text
                            stringByTrimmingCharactersInSet:[NSCharacterSet
                                                                whitespaceAndNewlineCharacterSet]];
                        if (trimmed.length == 0 || !self.appContext) {
                          return;
                        }
                        auto* app_context = self.appContext;
                        if (!app_context) {
                          return;
                        }
                        self.statusLabel.text = @"Creating profile...";
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
                          [failure addAction:[UIAlertAction actionWithTitle:@"OK"
                                                                      style:UIAlertActionStyleCancel
                                                                    handler:nil]];
                          [self presentViewController:failure animated:YES completion:nil];
                          self.statusLabel.text = @"Profile creation failed.";
                          return;
                        }
                        BOOL signed_in = app_context->SignInProfile(xuid);
                        if (!signed_in) {
                          self.statusLabel.text = @"Failed to sign in with the new profile.";
                          return;
                        }
                        [self refreshSignedInProfileUI];
                        self.statusLabel.text =
                            [NSString stringWithFormat:@"Signed in as %@.", gamertag];
                      }]];
  [self presentViewController:create_alert animated:YES completion:nil];
}

- (void)openProfileTapped:(UIButton*)sender {
  if (!self.appContext) {
    return;
  }

  XeniaProfileViewController* profile_vc =
      [[XeniaProfileViewController alloc] initWithAppContext:self.appContext
                                                    onStatus:^(NSString* status_message) {
                                                      [self refreshSignedInProfileUI];
                                                      if (status_message.length > 0) {
                                                        self.statusLabel.text = status_message;
                                                      }
                                                    }];
  XeniaLandscapeNavigationController* nav =
      [[XeniaLandscapeNavigationController alloc] initWithRootViewController:profile_vc];
  if (UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad) {
    nav.modalPresentationStyle = UIModalPresentationFormSheet;
    if (@available(iOS 15.0, *)) {
      UISheetPresentationController* sheet = nav.sheetPresentationController;
      sheet.detents = @[
        [UISheetPresentationControllerDetent mediumDetent],
        [UISheetPresentationControllerDetent largeDetent]
      ];
      sheet.prefersGrabberVisible = YES;
    }
    UIPopoverPresentationController* popover = nav.popoverPresentationController;
    if (popover) {
      popover.sourceView = sender ?: self.profileButton;
      popover.sourceRect = (sender ?: self.profileButton).bounds;
    }
  } else {
    nav.modalPresentationStyle = UIModalPresentationFullScreen;
  }
  [self presentViewController:nav animated:YES completion:nil];
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
        addAction:[UIAlertAction
                      actionWithTitle:@"Create"
                                style:UIAlertActionStyleDefault
                              handler:^(__unused UIAlertAction* action) {
                                UITextField* text_field = create_alert.textFields.firstObject;
                                NSString* raw_text = text_field.text ?: @"";
                                NSString* trimmed = [raw_text stringByTrimmingCharactersInSet:
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
                                  if (self.statusLabel) {
                                    self.statusLabel.text =
                                        @"Profile could not be created. Please try again.";
                                  }
                                  finish(NO);
                                  return;
                                }
                                BOOL signed_in = app_context->SignInProfile(xuid);
                                if (signed_in) {
                                  self.statusLabel.text =
                                      [NSString stringWithFormat:@"Signed in as %@.", gamertag];
                                } else if (self.statusLabel) {
                                  self.statusLabel.text = @"Failed to sign in with the new profile.";
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

  NSString* message = [NSString stringWithFormat:@"Select profile (needs %u user%@).",
                                                 users_needed, users_needed == 1 ? @"" : @"s"];
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
    popover.sourceRect = CGRectMake(CGRectGetMidX(self.view.bounds), CGRectGetMidY(self.view.bounds),
                                    1.0, 1.0);
    popover.permittedArrowDirections = 0;
  }

  UIViewController* presenter = self;
  while (presenter.presentedViewController) {
    presenter = presenter.presentedViewController;
  }
  [presenter presentViewController:sheet animated:YES completion:nil];
}

- (void)presentSystemKeyboardPromptWithTitle:(NSString*)title
                                 description:(NSString*)description
                                 defaultText:(NSString*)default_text
                                  completion:(void (^)(BOOL cancelled,
                                                       NSString* text))completion {
  UIAlertController* alert = [UIAlertController alertControllerWithTitle:title.length
                                                                       ? title
                                                                       : @"Input Required"
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
  // The banner is the single JIT indicator — dot color updates but the
  // banner itself hides entirely when JIT is acquired.
  if (self.jitAcquired) {
    self.jitStatusDot.backgroundColor = [XeniaTheme accent];
    self.jitStatusLabel.text = @"JIT Enabled";
  } else {
    self.jitStatusDot.backgroundColor = [XeniaTheme statusWarning];
    self.jitStatusLabel.text = @"JIT Not Detected";
  }
}

- (void)updateJITAvailabilityUI {
  BOOL previous_hidden = self.jitWarningCard.hidden;
  BOOL jit_ready = self.jitAcquired;
  self.jitWarningCard.hidden = jit_ready;
  if (previous_hidden != self.jitWarningCard.hidden) {
    [self.importedGamesCollectionView.collectionViewLayout invalidateLayout];
  }
  self.openGameButton.enabled = YES;
  self.openGameButton.alpha = 1.0;
}

- (std::filesystem::path)importedGamesDirectory {
  return xe_get_ios_documents_path() / "games";
}

- (std::filesystem::path)importGameIntoLibrary:(NSURL*)source_url error:(NSError**)error {
  std::filesystem::path source_path([source_url.path UTF8String]);
  std::filesystem::path library_path = [self importedGamesDirectory];

  std::error_code ec;
  std::filesystem::create_directories(library_path, ec);
  if (ec) {
    if (error) {
      *error = [NSError
          errorWithDomain:@"XeniaIOSImport"
                     code:1001
                 userInfo:@{
                   NSLocalizedDescriptionKey : [NSString
                       stringWithFormat:@"Failed creating library folder: %s", ec.message().c_str()]
                 }];
    }
    return {};
  }

  auto weak_source = std::filesystem::weakly_canonical(source_path, ec);
  auto weak_library = std::filesystem::weakly_canonical(library_path, ec);
  if (!ec && weak_source.native().rfind(weak_library.native(), 0) == 0) {
    return weak_source;
  }

  std::filesystem::path destination = library_path / source_path.filename();
  std::filesystem::path stem = destination.stem();
  std::filesystem::path extension = destination.extension();
  for (int attempt = 2; std::filesystem::exists(destination); ++attempt) {
    destination =
        library_path / std::filesystem::path(stem.string() + " (" + std::to_string(attempt) + ")" +
                                             extension.string());
  }

  NSString* source_ns = source_url.path;
  NSString* destination_ns = ToNSString(destination.string());
  if (![[NSFileManager defaultManager] copyItemAtPath:source_ns
                                               toPath:destination_ns
                                                error:error]) {
    return {};
  }

  if (HasContentSidecarDataDirectory(source_path)) {
    std::filesystem::path source_sidecar = source_path;
    source_sidecar += ".data";
    std::filesystem::path destination_sidecar = destination;
    destination_sidecar += ".data";

    std::string error_message;
    if (!xe_copy_directory_recursive(source_sidecar, destination_sidecar, &error_message)) {
      std::error_code cleanup_error;
      std::filesystem::remove(destination, cleanup_error);
      std::filesystem::remove_all(destination_sidecar, cleanup_error);
      if (error) {
        *error = [NSError
            errorWithDomain:@"XeniaIOSImport"
                       code:1002
                   userInfo:@{
                     NSLocalizedDescriptionKey : ToNSString(
                         error_message.empty() ? "Failed copying package sidecar." : error_message)
                   }];
      }
      return {};
    }
  }

  return destination;
}

- (void)refreshImportedGames {
  discovered_games_.clear();

  // Load cached title names populated by previous game launches.
  NSString* caches_dir = NSSearchPathForDirectoriesInDomains(
      NSCachesDirectory, NSUserDomainMask, YES).firstObject;
  NSString* names_path = [caches_dir stringByAppendingPathComponent:@"title-names.plist"];
  NSDictionary* title_name_cache =
      [[NSDictionary dictionaryWithContentsOfFile:names_path] retain];

  std::vector<std::filesystem::path> scan_roots;
  const std::filesystem::path documents_root = xe_get_ios_documents_path();
  const std::filesystem::path library_root = [self importedGamesDirectory];
  scan_roots.push_back(library_root);
  if (documents_root != library_root) {
    scan_roots.push_back(documents_root);
  }

  // Format priority for dedup: GOD (full metadata + content) > ISO (full
  // disc filesystem) > standalone XEX (single executable, missing disc files).
  auto format_priority = [](const std::filesystem::path& p) -> int {
    if (IsLikelyGodContainerFile(p)) return 0;
    if (IsISOPath(p)) return 1;
    return 2;
  };

  std::set<std::filesystem::path> seen_paths;
  std::map<uint32_t, size_t> title_id_to_index;
  for (const auto& root : scan_roots) {
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) {
      continue;
    }

    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    std::filesystem::recursive_directory_iterator end;
    while (!ec && it != end) {
      const auto& entry = *it;
      const auto filename = entry.path().filename().string();
      const auto filename_lower = ToLowerAsciiCopy(filename);
      if (entry.is_directory(ec)) {
        if (filename_lower == "cache" || filename_lower == "cache_host") {
          it.disable_recursion_pending();
        }
      } else if (entry.is_regular_file(ec) &&
                 (IsISOPath(entry.path()) || IsDefaultXexPath(entry.path()) ||
                  IsLikelyGodContainerFile(entry.path()))) {
        const std::filesystem::path canonical_path =
            std::filesystem::weakly_canonical(entry.path(), ec);
        const std::filesystem::path unique_path =
            ec ? std::filesystem::absolute(entry.path(), ec) : canonical_path;
        ec.clear();

        if (seen_paths.insert(unique_path).second) {
          IOSDiscoveredGame game;
          if (!BuildDiscoveredGameFromPath(unique_path, &game)) {
            ++it;
            continue;
          }
          if (game.title_id && title_name_cache) {
            NSString* key = [NSString stringWithFormat:@"%08x", game.title_id];
            NSString* cached = [title_name_cache objectForKey:key];
            if (cached.length > 0) {
              game.title = NormalizeGameTitleForUI(
                  std::string([cached UTF8String]));
            }
          }
          if (game.title_id) {
            auto existing = title_id_to_index.find(game.title_id);
            if (existing != title_id_to_index.end()) {
              int old_pri = format_priority(
                  discovered_games_[existing->second].path);
              int new_pri = format_priority(unique_path);
              if (new_pri < old_pri) {
                discovered_games_[existing->second] = std::move(game);
              }
              ++it;
              continue;
            }
            title_id_to_index[game.title_id] = discovered_games_.size();
          }
          discovered_games_.push_back(std::move(game));
        }
      }

      ++it;
    }
  }

  [title_name_cache release];

  for (auto& game : discovered_games_) {
    if (!game.title_id) {
      continue;
    }
    std::error_code ec;
    if (std::filesystem::exists(xe_title_content_root(game.title_id), ec)) {
      game.has_installed_content = true;
    }
  }

  [self applyCompatDataToDiscoveredGames];

  std::sort(discovered_games_.begin(), discovered_games_.end(),
            [](const IOSDiscoveredGame& a, const IOSDiscoveredGame& b) {
              if (a.title == b.title) {
                return a.path.filename().string() < b.path.filename().string();
              }
              return a.title < b.title;
            });

  [self.importedGamesCollectionView reloadData];
  self.importedGamesEmptyLabel.hidden = !discovered_games_.empty();

  if (discovered_games_.empty()) {
    focused_game_index_ = -1;
  } else if (focused_game_index_ < 0 ||
             focused_game_index_ >= static_cast<NSInteger>(discovered_games_.size())) {
    focused_game_index_ = 0;
  }
  [self rebuildLauncherFocusGraph];
  [self applyLauncherFocusVisuals];
}

- (void)applyCompatDataToDiscoveredGames {
  for (auto& game : discovered_games_) {
    game.has_compat_info = false;
    game.compat_status.clear();
    game.compat_perf.clear();
    game.compat_notes.clear();
    if (!compat_data_ || !game.title_id) {
      continue;
    }
    NSString* key = [NSString stringWithFormat:@"%08X", game.title_id];
    NSDictionary* info = [compat_data_ objectForKey:key];
    if (!info) {
      continue;
    }
    NSString* status = info[@"status"];
    NSString* perf = info[@"perf"];
    NSString* notes = info[@"notes"];
    if ([status isKindOfClass:[NSString class]] && status.length > 0) {
      game.has_compat_info = true;
      game.compat_status = std::string([status UTF8String]);
      game.compat_perf =
          [perf isKindOfClass:[NSString class]]
              ? std::string([perf UTF8String])
              : "";
      game.compat_notes =
          [notes isKindOfClass:[NSString class]]
              ? std::string([notes UTF8String])
              : "";
    }
  }
}

- (void)presentJITRequiredAlert {
  UIAlertController* alert = [UIAlertController
      alertControllerWithTitle:@"JIT Not Detected"
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

- (BOOL)handleExternalLaunchURL:(NSURL*)url {
  std::filesystem::path launch_path;
  if (!ExtractLaunchPathFromExternalURL(url, &launch_path) ||
      launch_path.empty()) {
    NSString* absolute_url = [url absoluteString];
    XELOGW("iOS: External launch URL missing path: {}",
           absolute_url ? [absolute_url UTF8String] : "");
    self.statusLabel.text = @"Launch URL missing game path.";
    return NO;
  }

  XELOGI("iOS: External game launch requested: {}", launch_path.string());
  if (!self.jitAcquired) {
    pending_external_launch_path_ = launch_path;
    self.statusLabel.text =
        [NSString stringWithFormat:@"Waiting for JIT to launch: %@",
                                   ToNSString(launch_path.filename().string())];
    return YES;
  }

  [self launchGameAtPath:launch_path
             displayName:ToNSString(launch_path.filename().string())];
  return YES;
}

- (void)launchGameAtPath:(const std::filesystem::path&)game_path
             displayName:(NSString*)display_name {
  NSString* path_ns = ToNSString(game_path.string());
  NSString* fallback_name = ToNSString(game_path.filename().string());
  NSString* game_label = display_name.length ? display_name : fallback_name;

  if (IsLikelyGodContainerFile(game_path)) {
    auto header = xe::vfs::XContentContainerDevice::ReadContainerHeader(game_path);
    if (header && header->content_metadata.data_file_count > 0 &&
        !HasContentSidecarDataDirectory(game_path)) {
      self.statusLabel.text = @"Selected game is missing its .data folder.";
      xe_present_ok_alert(
          self, @"Missing Game Data",
          @"This package needs its matching .data folder before it can be launched.");
      return;
    }
  }

  if (!self.jitAcquired) {
    [self presentJITRequiredAlert];
    return;
  }

  if (self.gameStopInProgress || self.gameRunning) {
    if (self.appContext) {
      self.statusLabel.text = [NSString stringWithFormat:@"Stopping current game; queued %@.",
                                                         game_label];
      self.appContext->LaunchGame(std::string([path_ns UTF8String]));
    } else {
      self.statusLabel.text = @"Unable to queue launch (app context unavailable).";
    }
    return;
  }

  self.statusLabel.text = [NSString stringWithFormat:@"Loading: %@", game_label];
  self.gameRunning = YES;

  xe_request_landscape_orientation(self);
  [UIView animateWithDuration:0.3
      animations:^{
        self.launcherOverlay.alpha = 0.0;
      }
      completion:^(__unused BOOL finished) {
        self.launcherOverlay.hidden = YES;
      }];

  if (self.appContext) {
    self.appContext->LaunchGame(std::string([path_ns UTF8String]));
  } else {
    self.statusLabel.text = @"Unable to launch game (app context unavailable).";
    self.launcherOverlay.hidden = NO;
    self.launcherOverlay.alpha = 1.0;
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

- (void)presentCompatibilitySheetForIndex:(size_t)game_index {
  if (game_index >= discovered_games_.size()) {
    return;
  }

  const IOSDiscoveredGame& game = discovered_games_[game_index];
  if (!game.title_id) {
    xe_present_ok_alert(self, @"Unavailable",
                        @"This item does not expose a title ID, so compatibility details "
                        @"cannot be loaded.");
    return;
  }

  NSDictionary* compat_data =
      [compat_data_ objectForKey:[NSString stringWithFormat:@"%08X", game.title_id]];
  NSString* game_title =
      game.title.empty() ? ToNSString(game.path.stem().string()) : ToNSString(game.title);
  UIImage* hero_artwork = xe_cached_game_art(game.title_id);
  if (!hero_artwork && !game.icon_data.empty()) {
    NSData* data = [NSData dataWithBytes:game.icon_data.data() length:game.icon_data.size()];
    hero_artwork = [UIImage imageWithData:data];
  }
  if (!hero_artwork && self.importedGamesCollectionView) {
    NSIndexPath* index_path = [NSIndexPath indexPathForItem:(NSInteger)game_index inSection:0];
    XeniaGameTileCell* tile =
        (XeniaGameTileCell*)[self.importedGamesCollectionView cellForItemAtIndexPath:index_path];
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
  if (UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad) {
    navigation_controller.modalPresentationStyle = UIModalPresentationFormSheet;
    if (@available(iOS 15.0, *)) {
      UISheetPresentationController* sheet = navigation_controller.sheetPresentationController;
      sheet.detents = @[
        [UISheetPresentationControllerDetent mediumDetent],
        [UISheetPresentationControllerDetent largeDetent]
      ];
      sheet.prefersGrabberVisible = YES;
    }
  } else {
    navigation_controller.modalPresentationStyle = UIModalPresentationFullScreen;
  }
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
    xe_present_ok_alert(
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
  if (UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad) {
    navigation_controller.modalPresentationStyle = UIModalPresentationFormSheet;
    if (@available(iOS 15.0, *)) {
      UISheetPresentationController* sheet = navigation_controller.sheetPresentationController;
      sheet.detents = @[
        [UISheetPresentationControllerDetent mediumDetent],
        [UISheetPresentationControllerDetent largeDetent]
      ];
      sheet.prefersGrabberVisible = YES;
    }
  } else {
    navigation_controller.modalPresentationStyle = UIModalPresentationFullScreen;
  }
  [self presentViewController:navigation_controller animated:YES completion:nil];
  [navigation_controller release];
  [content_controller release];
}

#pragma mark - UICollectionViewDataSource

- (NSInteger)collectionView:(UICollectionView* __unused)collectionView
     numberOfItemsInSection:(NSInteger)__unused section {
  return static_cast<NSInteger>(discovered_games_.size());
}

- (UICollectionViewCell*)collectionView:(UICollectionView*)collectionView
                 cellForItemAtIndexPath:(NSIndexPath*)indexPath {
  XeniaGameTileCell* cell =
      [collectionView dequeueReusableCellWithReuseIdentifier:@"ImportedGameCell"
                                                forIndexPath:indexPath];
  if (indexPath.item < 0 || static_cast<size_t>(indexPath.item) >= discovered_games_.size()) {
    cell.titleLabel.text = @"";
    cell.iconView.image = nil;
    cell.controllerFocused = NO;
    return cell;
  }

  const IOSDiscoveredGame& game = discovered_games_[static_cast<size_t>(indexPath.item)];
  NSString* title =
      game.title.empty() ? ToNSString(game.path.stem().string()) : ToNSString(game.title);
  cell.titleLabel.text = title;
  if (game.has_compat_info) {
    NSString* status = ToNSString(game.compat_status);
    UIColor* pill_color = xe_compat_status_color(status);
    cell.compatPill.text = xe_compat_status_label(status);
    cell.compatPill.textColor = pill_color;
    cell.compatPill.backgroundColor =
        [pill_color colorWithAlphaComponent:0.1];
    cell.compatPill.hidden = NO;
  } else {
    cell.compatPill.text = @"";
    cell.compatPill.hidden = YES;
  }
  cell.controllerFocused = controller_navigation_was_enabled_ &&
                           launcher_library_focus_active_ &&
                           focused_game_index_ == indexPath.item;

  // Priority: cached remote art → async fetch → embedded icon → placeholder.
  // Remote tile.png is much higher resolution than embedded 64x64 icons.
  UIImage* icon = xe_cached_game_art(game.title_id);
  if (icon) {
    cell.iconView.image = icon;
  } else {
    // Show embedded icon (or placeholder) while fetching high-res art.
    UIImage* fallback = nil;
    if (!game.icon_data.empty()) {
      NSData* data = [NSData dataWithBytes:game.icon_data.data() length:game.icon_data.size()];
      fallback = [UIImage imageWithData:data];
    }
    if (!fallback) fallback = [UIImage imageNamed:@"128"];
    if (!fallback) fallback = [UIImage systemImageNamed:@"gamecontroller.fill"];
    cell.iconView.image = fallback;
    if (game.title_id) {
      uint32_t fetch_title_id = game.title_id;
      // No __weak under MRC — collectionView is owned by self and won't
      // be deallocated while the launcher overlay is visible.
      UICollectionView* cv = collectionView;
      xe_fetch_game_art(fetch_title_id, ^(UIImage* fetched) {
        if (!fetched || !cv) return;
        NSMutableArray* reload_paths = [NSMutableArray array];
        for (size_t i = 0; i < self->discovered_games_.size(); ++i) {
          if (self->discovered_games_[i].title_id == fetch_title_id) {
            [reload_paths addObject:[NSIndexPath indexPathForItem:static_cast<NSInteger>(i)
                                                         inSection:0]];
          }
        }
        if (reload_paths.count > 0) {
          [cv reloadItemsAtIndexPaths:reload_paths];
        }
      });
    }
  }
  return cell;
}

#pragma mark - UICollectionViewDelegate

- (void)collectionView:(UICollectionView*)collectionView
    didSelectItemAtIndexPath:(NSIndexPath*)indexPath {
  [collectionView deselectItemAtIndexPath:indexPath animated:YES];
  if (indexPath.item < 0 || static_cast<size_t>(indexPath.item) >= discovered_games_.size()) {
    return;
  }
  [self setFocusedGameIndex:indexPath.item scroll:NO];
  const IOSDiscoveredGame& game = discovered_games_[static_cast<size_t>(indexPath.item)];
  [self launchGameAtPath:game.path displayName:ToNSString(game.title)];
}

- (UIContextMenuConfiguration*)collectionView:(UICollectionView*)collectionView
    contextMenuConfigurationForItemAtIndexPath:(NSIndexPath*)indexPath
                                         point:(CGPoint)point {
  (void)collectionView;
  (void)point;
  if (indexPath.item < 0 ||
      static_cast<size_t>(indexPath.item) >= discovered_games_.size()) {
    return nil;
  }

  const size_t game_index = static_cast<size_t>(indexPath.item);
  return [UIContextMenuConfiguration
      configurationWithIdentifier:nil
                  previewProvider:nil
                   actionProvider:^UIMenu*(
                       NSArray<UIMenuElement*>* __unused suggested_actions) {
                     const IOSDiscoveredGame& game =
                         self->discovered_games_[game_index];
                     const std::filesystem::path game_path = game.path;
                     NSString* game_title = ToNSString(game.title);
                     const BOOL can_manage_content = game.title_id != 0;
                     const BOOL can_view_compatibility = game.title_id != 0;
                     UIAction* play_action = [UIAction
                         actionWithTitle:@"Play"
                                   image:[UIImage systemImageNamed:@"play.fill"]
                              identifier:nil
                                 handler:^(__unused UIAction* action) {
                                   [self launchGameAtPath:game_path displayName:game_title];
                                 }];
                     UIAction* compatibility_action =
                         [UIAction actionWithTitle:@"Compatibility"
                                             image:[UIImage systemImageNamed:@"checkmark.shield"]
                                        identifier:nil
                                           handler:^(__unused UIAction* action) {
                                             [self presentCompatibilitySheetForIndex:game_index];
                                           }];
                     UIAction* content_action =
                         [UIAction actionWithTitle:@"Manage Content"
                                             image:[UIImage systemImageNamed:@"square.stack.3d.up"]
                                        identifier:nil
                                           handler:^(__unused UIAction* action) {
                                             [self presentManageContentSheetForIndex:game_index];
                                           }];
                     if (!can_view_compatibility) {
                       compatibility_action.attributes = UIMenuElementAttributesDisabled;
                     }
                     if (!can_manage_content) {
                       content_action.attributes = UIMenuElementAttributesDisabled;
                     }
                     return [UIMenu
                         menuWithTitle:@""
                              children:@[ play_action, compatibility_action, content_action ]];
                   }];
}

#pragma mark - UICollectionViewDelegateFlowLayout

- (CGSize)collectionView:(UICollectionView*)collectionView
                    layout:(UICollectionViewLayout* __unused)collectionViewLayout
    sizeForItemAtIndexPath:(NSIndexPath* __unused)indexPath {
  CGFloat content_width = collectionView.bounds.size.width;
  NSInteger columns = [self launcherGridColumnCountForContentSize:collectionView.bounds.size];
  CGFloat spacing = 16.0f;
  if ([collectionView.collectionViewLayout isKindOfClass:[UICollectionViewFlowLayout class]]) {
    spacing = [(UICollectionViewFlowLayout*)collectionView.collectionViewLayout
        minimumInteritemSpacing];
  }
  CGFloat total_spacing = spacing * (columns - 1);
  CGFloat tile_width = floor((content_width - total_spacing) / columns);
  tile_width = MAX(tile_width, 100.0f);
  // Cover art is ~219x300 (~1:1.37). Reserve enough room for a readable
  // two-line title strip and a compat pill on its own row.
  CGFloat image_height = floor(tile_width * 300.0f / 219.0f);
  return CGSizeMake(tile_width, image_height + 78.0f);
}

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
  CGSize collection_size = self.importedGamesCollectionView.bounds.size;
  if (!CGSizeEqualToSize(collection_size, last_collection_layout_size_)) {
    last_collection_layout_size_ = collection_size;
    [self.importedGamesCollectionView.collectionViewLayout invalidateLayout];
  }
  // Notify the app context that the layout changed, so the window and
  // presenter can update for rotation, split-view, or safe-area changes.
  if (self.appContext) {
    self.appContext->NotifyLayoutChanged();
  }
}

- (void)openGameTapped:(UIButton*)sender {
  if (self.gameStopInProgress) {
    self.statusLabel.text = @"Stopping game... Please wait.";
    return;
  }
  NSArray<UTType*>* contentTypes = @[
    [UTType typeWithFilenameExtension:@"iso"],
    [UTType typeWithFilenameExtension:@"xex"],
    UTTypeData,
  ];

  UIDocumentPickerViewController* picker =
      [[UIDocumentPickerViewController alloc]
          initForOpeningContentTypes:contentTypes];
  picker.delegate = self;
  picker.allowsMultipleSelection = NO;
  picker.shouldShowFileExtensions = YES;
  [self presentViewController:picker animated:YES completion:nil];
}

- (void)openSettingsTapped:(UIButton*)sender {
  XeniaConfigViewController* settings_vc =
      [[XeniaConfigViewController alloc] initWithStyle:UITableViewStyleInsetGrouped];
  XeniaLandscapeNavigationController* nav =
      [[XeniaLandscapeNavigationController alloc] initWithRootViewController:settings_vc];
  if (UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad) {
    nav.modalPresentationStyle = UIModalPresentationFormSheet;
    if (@available(iOS 15.0, *)) {
      UISheetPresentationController* sheet = nav.sheetPresentationController;
      sheet.detents = @[
        [UISheetPresentationControllerDetent mediumDetent],
        [UISheetPresentationControllerDetent largeDetent]
      ];
      sheet.prefersGrabberVisible = YES;
    }
  } else {
    nav.modalPresentationStyle = UIModalPresentationFullScreen;
  }
  [self presentViewController:nav animated:YES completion:nil];
}

#pragma mark - UIDocumentPickerDelegate

- (void)documentPicker:(UIDocumentPickerViewController* __unused)controller
    didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
  if (urls.count == 0) return;

  NSURL* url = urls[0];
  BOOL access_granted = [url startAccessingSecurityScopedResource];
  XELOGI("iOS: User selected game file: {} (security-scoped: {})", [url.path UTF8String],
         access_granted ? "yes" : "no");

  void (^import_selected_game)(void) = ^{
    NSError* import_error = nil;
    std::filesystem::path imported_path = [self importGameIntoLibrary:url error:&import_error];
    if (access_granted) {
      [url stopAccessingSecurityScopedResource];
    }

    if (imported_path.empty()) {
      NSString* message = import_error.localizedDescription ?: @"Failed to import selected game.";
      xe_present_ok_alert(self, @"Import Failed", message);
      return;
    }

    [self refreshImportedGames];
    NSString* imported_name = ToNSString(imported_path.filename().string());
    if (self.jitAcquired) {
      [self launchGameAtPath:imported_path displayName:imported_name];
    } else {
      self.statusLabel.text =
          [NSString stringWithFormat:@"Imported %@. Waiting for JIT.", imported_name];
    }
  };

  const std::filesystem::path selected_path([url.path UTF8String]);
  const BOOL likely_direct_game = IsISOPath(selected_path) || IsDefaultXexPath(selected_path);
  IOSSelectedContentPackage package_info;
  const BOOL has_content_package_info =
      xe_read_selected_content_package(selected_path, &package_info, nullptr);
  const BOOL is_launchable_package =
      has_content_package_info && (package_info.content_type == xe::XContentType::kXbox360Title ||
                                   package_info.content_type == xe::XContentType::kInstalledGame);
  const BOOL should_try_title_update_install = cvars::ios_async_import_ui && self.appContext &&
                                               !likely_direct_game && !is_launchable_package;
  if (should_try_title_update_install) {
    self.statusLabel.text = @"Checking content package...";
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
      std::string status;
      bool not_title_update = false;
      bool success = self.appContext->InstallTitleUpdate(std::string([url.path UTF8String]),
                                                         &status, &not_title_update);

      dispatch_async(dispatch_get_main_queue(), ^{
        if (success) {
          if (access_granted) {
            [url stopAccessingSecurityScopedResource];
          }
          NSString* message = status.empty() ? @"Installed title update." : ToNSString(status);
          self.statusLabel.text = message;
          [self refreshImportedGames];
          xe_present_ok_alert(self, @"Title Update Installed", message);
          return;
        }

        if (!not_title_update) {
          if (access_granted) {
            [url stopAccessingSecurityScopedResource];
          }
          NSString* message =
              status.empty() ? @"Title update installation failed." : ToNSString(status);
          self.statusLabel.text = message;
          xe_present_ok_alert(self, @"Installation Failed", message);
          return;
        }

        import_selected_game();
      });
    });
    return;
  }

  import_selected_game();
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController* __unused)controller {
  XELOGI("iOS: Document picker cancelled");
}

#pragma mark - Status bar / home indicator

- (BOOL)prefersStatusBarHidden {
  return YES;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
  if (self.launcherOverlay.hidden) {
    return UIInterfaceOrientationMaskLandscape;
  }
  if (!self.launcherLandscapeUnlocked) {
    return UIInterfaceOrientationMaskPortrait;
  }
  return UIInterfaceOrientationMaskAllButUpsideDown;
}

- (UIInterfaceOrientation)preferredInterfaceOrientationForPresentation {
  return UIInterfaceOrientationPortrait;
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
  self.gameRunning = NO;
  self.gameStopInProgress = NO;
  [self hideInGameMenuOverlay];
  self.launcherOverlay.hidden = NO;
  self.statusLabel.text = @"";
  [self refreshImportedGames];
  [self refreshSignedInProfileUI];
  [self updateJITStatusIndicator];
  [self updateJITAvailabilityUI];
  [self rebuildLauncherFocusGraph];
  [self applyLauncherFocusVisuals];
  xe_request_portrait_orientation(self);
  [UIView animateWithDuration:0.3
                   animations:^{
                     self.launcherOverlay.alpha = 1.0;
                   }];
}

- (void)dealloc {
  [self.jitPollTimer invalidate];
  [self.controllerNavTimer invalidate];
  [compat_data_ release];
}

@end

// ---------------------------------------------------------------------------
// XeniaAppDelegate - UIKit application lifecycle.
// ---------------------------------------------------------------------------
@interface XeniaAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
- (BOOL)bootstrapApplicationWithWindow:(UIWindow*)window
                             launchURL:(NSURL*)launch_url
                             sourceTag:(const char*)source_tag;
- (XeniaViewController*)xeniaViewController;
- (BOOL)handleExternalLaunchURL:(NSURL*)url sourceTag:(const char*)source_tag;
@end

@implementation XeniaAppDelegate {
  std::unique_ptr<xe::ui::IOSWindowedAppContext> app_context_;
  std::unique_ptr<xe::ui::WindowedApp> app_;
}

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
  (void)application;
  NSURL* launch_url = nil;
  if (launchOptions) {
    launch_url = [launchOptions objectForKey:UIApplicationLaunchOptionsURLKey];
  }
  if (@available(iOS 13.0, *)) {
    if (launch_url) {
      XELOGI("iOS: launch URL deferred to scene bootstrap");
    }
    return YES;
  }

  UIWindow* legacy_window =
      [[[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]]
          autorelease];
  return [self bootstrapApplicationWithWindow:legacy_window
                                    launchURL:launch_url
                                    sourceTag:"launchOptions"];
}

- (BOOL)bootstrapApplicationWithWindow:(UIWindow*)window
                             launchURL:(NSURL*)launch_url
                             sourceTag:(const char*)source_tag {
  if (!window) {
    XELOGE("iOS: Cannot bootstrap app without a UIWindow");
    return NO;
  }
  UIWindow* previous_window = self.window;
  self.window = window;
  if (app_) {
    UIViewController* existing_root = previous_window.rootViewController;
    if (!self.window.rootViewController && existing_root) {
      self.window.rootViewController = existing_root;
      [self.window makeKeyAndVisible];
    }
    if (launch_url) {
      [self handleExternalLaunchURL:launch_url
                          sourceTag:source_tag ? source_tag : "bootstrap"];
    }
    return YES;
  }

  // Initialize cvars with no arguments on iOS (arguments come from config).
  int argc = 1;
  char arg0[] = "xenia_edge";
  char* argv[] = {arg0};
  char** argv_ptr = argv;
  cvar::ParseLaunchArguments(argc, argv_ptr, "", {});

  // Create the app context.
  app_context_ = std::make_unique<xe::ui::IOSWindowedAppContext>();

  // Set up the UIKit window and view controller FIRST, so the Metal view
  // is available when the app initializes.
  XeniaViewController* vc = [[XeniaViewController alloc] init];
  self.window.rootViewController = vc;
  [self.window makeKeyAndVisible];
  xe_request_portrait_orientation(vc);

  // Force layout so the Metal view is created.
  [vc.view layoutIfNeeded];

  // Store the Metal view and view controller in the app context for
  // iOSWindow to use.
  app_context_->set_metal_view(vc.metalView);
  app_context_->set_view_controller(vc);
  vc.appContext = app_context_.get();
  app_context_->set_signin_ui_prompt_callback(
      [vc](uint32_t user_index, uint32_t users_needed) {
        if ([NSThread isMainThread]) {
          return false;
        }
        __block BOOL success = NO;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        dispatch_async(dispatch_get_main_queue(), ^{
          [vc presentSystemSigninPromptForUserIndex:user_index
                                        usersNeeded:users_needed
                                         completion:^(BOOL prompt_success) {
                                           success = prompt_success;
                                           dispatch_semaphore_signal(sem);
                                         }];
        });
        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
        return success ? true : false;
      });
  app_context_->set_keyboard_prompt_callback(
      [vc](const std::string& title, const std::string& description,
           const std::string& default_text, std::string* text_out,
           bool* cancelled_out) {
        if ([NSThread isMainThread]) {
          if (cancelled_out) {
            *cancelled_out = true;
          }
          return false;
        }
        __block BOOL cancelled = YES;
        __block NSString* typed_text = @"";
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        dispatch_async(dispatch_get_main_queue(), ^{
          [vc presentSystemKeyboardPromptWithTitle:ToNSString(title)
                                       description:ToNSString(description)
                                       defaultText:ToNSString(default_text)
                                        completion:^(BOOL prompt_cancelled,
                                                     NSString* prompt_text) {
                                          cancelled = prompt_cancelled;
                                          typed_text = prompt_text ?: @"";
                                          dispatch_semaphore_signal(sem);
                                        }];
        });
        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
        if (text_out) {
          *text_out = std::string([typed_text UTF8String]);
        }
        if (cancelled_out) {
          *cancelled_out = cancelled ? true : false;
        }
        return true;
      });
  app_context_->set_game_exited_callback([vc]() { [vc showLauncherOverlay]; });
  app_context_->set_profile_services_ready_callback([vc]() {
    dispatch_async(dispatch_get_main_queue(), ^{
      [vc refreshSignedInProfileUI];
      if ([vc.statusLabel.text isEqualToString:@"Initializing profile services..."]) {
        vc.statusLabel.text = @"";
      }
    });
  });

  XELOGI("iOS: Metal view ready ({}x{})",
         static_cast<uint32_t>(vc.metalView.bounds.size.width *
                               vc.metalView.contentScaleFactor),
         static_cast<uint32_t>(vc.metalView.bounds.size.height *
                               vc.metalView.contentScaleFactor));

  // Create and initialize the Xenia app.
  app_ = xe::ui::GetWindowedAppCreator()(*app_context_);
  if (cvars::log_file.empty()) {
    cvars::log_file = xe_get_ios_documents_path() / "xenia.log";
  }
  xe::InitializeLogging(app_->GetName(), true);

  if (!app_->OnInitialize()) {
    XELOGE("iOS: App initialization failed");
    return NO;
  }

  [vc refreshSignedInProfileUI];
  if (vc.appContext) {
    vc.statusLabel.text = @"Initializing profile services...";
    vc.appContext->LaunchGame(std::string());
  }

  if (launch_url) {
    [self handleExternalLaunchURL:launch_url
                        sourceTag:source_tag ? source_tag : "bootstrap"];
  }

  XELOGI("iOS: Application launched successfully");
  return YES;
}

- (XeniaViewController*)xeniaViewController {
  UIViewController* root_view_controller = self.window.rootViewController;
  if ([root_view_controller isKindOfClass:[XeniaViewController class]]) {
    return (XeniaViewController*)root_view_controller;
  }
  return nil;
}

- (BOOL)handleExternalLaunchURL:(NSURL*)url sourceTag:(const char*)source_tag {
  if (!url) {
    return NO;
  }

  NSString* absolute_url = [url absoluteString];
  XELOGI("iOS: Received app URL ({}) {}",
         source_tag ? source_tag : "unknown",
         absolute_url ? [absolute_url UTF8String] : "");

  XeniaViewController* view_controller = [self xeniaViewController];
  if (!view_controller) {
    XELOGW("iOS: Ignoring URL launch; root view controller unavailable");
    return NO;
  }

  BOOL handled = [view_controller handleExternalLaunchURL:url];
  if (!handled) {
    XELOGW("iOS: URL launch was not handled");
  }
  return handled ? YES : NO;
}

- (BOOL)application:(UIApplication*)application
            openURL:(NSURL*)url
            options:(NSDictionary<UIApplicationOpenURLOptionsKey, id>*)options {
  (void)application;
  (void)options;
  return [self handleExternalLaunchURL:url sourceTag:"openURL"];
}

- (UISceneConfiguration*)application:(UIApplication*)application
    configurationForConnectingSceneSession:(UISceneSession*)connectingSceneSession
                                    options:(UISceneConnectionOptions*)options {
  (void)application;
  (void)options;
  if (@available(iOS 13.0, *)) {
    UISceneConfiguration* configuration =
        [[[UISceneConfiguration alloc] initWithName:@"Default Configuration"
                                         sessionRole:connectingSceneSession.role]
            autorelease];
    configuration.delegateClass = [XeniaSceneDelegate class];
    return configuration;
  }
  return nil;
}

- (UIInterfaceOrientationMask)application:(UIApplication*)application
    supportedInterfaceOrientationsForWindow:(UIWindow*)window {
  UIViewController* root = window.rootViewController;
  if (root) {
    return [root supportedInterfaceOrientations];
  }
  return UIInterfaceOrientationMaskPortrait;
}

- (void)applicationWillTerminate:(UIApplication*)application {
  XELOGI("iOS lifecycle: applicationWillTerminate");
  if (app_) {
    app_->InvokeOnDestroy();
    app_.reset();
  }
  app_context_.reset();
}

@end

@interface XeniaSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation XeniaSceneDelegate

- (void)scene:(UIScene*)scene
    willConnectToSession:(UISceneSession*)session
                  options:(UISceneConnectionOptions*)connectionOptions {
  (void)session;
  if (![scene isKindOfClass:[UIWindowScene class]]) {
    XELOGE(
        "iOS: scene connection ignored because scene is not a UIWindowScene");
    return;
  }

  UIWindowScene* window_scene = (UIWindowScene*)scene;
  UIWindow* scene_window =
      [[[UIWindow alloc] initWithWindowScene:window_scene] autorelease];
  self.window = scene_window;

  NSURL* launch_url =
      xe_first_open_url_context_url(connectionOptions.URLContexts);
  XeniaAppDelegate* app_delegate =
      (XeniaAppDelegate*)[UIApplication sharedApplication].delegate;
  if (!app_delegate ||
      ![app_delegate bootstrapApplicationWithWindow:scene_window
                                          launchURL:launch_url
                                          sourceTag:"sceneConnect"]) {
    XELOGE("iOS: scene bootstrap failed");
  }
}

- (void)scene:(UIScene*)scene openURLContexts:(NSSet<UIOpenURLContext*>*)URLContexts {
  (void)scene;
  NSURL* url = xe_first_open_url_context_url(URLContexts);
  XeniaAppDelegate* app_delegate =
      (XeniaAppDelegate*)[UIApplication sharedApplication].delegate;
  if (app_delegate) {
    [app_delegate handleExternalLaunchURL:url sourceTag:"sceneOpenURL"];
  }
}

@end

// ---------------------------------------------------------------------------
// iOS entry point.
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
  NSLog(@"iOS: skipping ptrace/debugger JIT setup; using normal W^X app flow.");
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil,
                             NSStringFromClass([XeniaAppDelegate class]));
  }
}
