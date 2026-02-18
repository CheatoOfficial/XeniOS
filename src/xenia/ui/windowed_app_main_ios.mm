/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#import <MetalKit/MetalKit.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <TargetConditionals.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
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
@class XeniaViewController;
@class XeniaMetalView;

extern "C" int csops(pid_t pid, unsigned int ops, void* useraddr, size_t usersize);

#ifndef CS_OPS_STATUS
#define CS_OPS_STATUS 0
#endif
#ifndef CS_DEBUGGED
#define CS_DEBUGGED 0x10000000
#endif

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

std::string FormatTitleID(uint32_t title_id) {
  if (!title_id) {
    return std::string();
  }
  char buffer[9] = {};
  std::snprintf(buffer, sizeof(buffer), "%08X", title_id);
  return std::string(buffer);
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
    game.title = DisplayNameFromXexMetadata(path, metadata);
    *game_out = std::move(game);
    return true;
  }

  if (IsDefaultXexPath(path)) {
    auto metadata = xe::vfs::ExtractXexMetadata(path);
    if (metadata.has_value()) {
      game.title_id = metadata->title_id;
    }
    game.title = DisplayNameFromXexMetadata(path, metadata);
    *game_out = std::move(game);
    return true;
  }

  if (!IsLikelyGodPath(path)) {
    return false;
  }

  auto header = xe::vfs::XContentContainerDevice::ReadContainerHeader(path);
  if (!header || !header->content_header.is_magic_valid()) {
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

@interface XeniaChoiceListViewController : UITableViewController
- (instancetype)initWithTitle:(NSString*)title
                     subtitle:(NSString*)subtitle
                      choices:(const std::vector<IOSConfigChoice>&)choices
                selectedValue:(int64_t)selectedValue
                  onSelection:(IOSChoiceSelectionHandler)onSelection;
@end

@interface XeniaLogViewController : UIViewController
@end

@interface XeniaLandscapeNavigationController : UINavigationController
@end

@interface XeniaGameTileCell : UICollectionViewCell
@property(nonatomic, strong) UIImageView* iconView;
@property(nonatomic, strong) UILabel* titleLabel;
@end

@implementation XeniaGameTileCell

- (instancetype)initWithFrame:(CGRect)frame {
  self = [super initWithFrame:frame];
  if (!self) {
    return nil;
  }

  // No card background — artwork IS the tile. Clean, modern, content-forward.
  self.contentView.backgroundColor = [UIColor clearColor];

  self.iconView = [[UIImageView alloc] init];
  self.iconView.translatesAutoresizingMaskIntoConstraints = NO;
  self.iconView.contentMode = UIViewContentModeScaleAspectFill;
  self.iconView.clipsToBounds = YES;
  self.iconView.layer.cornerRadius = 10;
  self.iconView.backgroundColor = [XeniaTheme bgSurface];
  [self.contentView addSubview:self.iconView];

  self.titleLabel = [[UILabel alloc] init];
  self.titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
  self.titleLabel.font = [UIFont systemFontOfSize:13 weight:UIFontWeightRegular];
  self.titleLabel.textColor = [XeniaTheme textSecondary];
  self.titleLabel.textAlignment = NSTextAlignmentLeft;
  self.titleLabel.numberOfLines = 1;
  self.titleLabel.lineBreakMode = NSLineBreakByTruncatingTail;
  [self.contentView addSubview:self.titleLabel];

  [NSLayoutConstraint activateConstraints:@[
    [self.iconView.topAnchor constraintEqualToAnchor:self.contentView.topAnchor],
    [self.iconView.leadingAnchor constraintEqualToAnchor:self.contentView.leadingAnchor],
    [self.iconView.trailingAnchor constraintEqualToAnchor:self.contentView.trailingAnchor],
    // Cover art aspect ratio ~219:300.
    [self.iconView.heightAnchor constraintEqualToAnchor:self.iconView.widthAnchor
                                             multiplier:300.0 / 219.0],
    [self.titleLabel.topAnchor constraintEqualToAnchor:self.iconView.bottomAnchor constant:6],
    [self.titleLabel.leadingAnchor constraintEqualToAnchor:self.contentView.leadingAnchor
                                                  constant:2],
    [self.titleLabel.trailingAnchor constraintEqualToAnchor:self.contentView.trailingAnchor
                                                   constant:-2],
  ]];

  return self;
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
                                                   UICollectionViewDelegateFlowLayout>
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
@property(nonatomic, assign) BOOL gameRunning;
@property(nonatomic, assign) BOOL gameStopInProgress;
@property(nonatomic, assign) xe::ui::IOSWindowedAppContext* appContext;

// JIT status widgets.
@property(nonatomic, strong) UIView* jitWarningCard;
@property(nonatomic, strong) UIView* jitStatusDot;
@property(nonatomic, strong) UILabel* jitStatusLabel;
@property(nonatomic, strong) NSTimer* jitPollTimer;
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
@end

@implementation XeniaViewController {
  std::vector<IOSDiscoveredGame> discovered_games_;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = [UIColor blackColor];
  self.jitAcquired = NO;
  self.launcherLandscapeUnlocked = NO;
  self.gameRunning = NO;
  self.gameStopInProgress = NO;

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
  [self refreshImportedGames];

  // Start polling for JIT.
  [self startJITPoll];
}

- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
  xe_request_portrait_orientation(self);
  if (!self.launcherLandscapeUnlocked) {
    self.launcherLandscapeUnlocked = YES;
    [self setNeedsUpdateOfSupportedInterfaceOrientations];
  }
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
  gridLayout.minimumInteritemSpacing = 14;
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
  UIButton* resume = [UIButton buttonWithConfiguration:resume_config primaryAction:nil];
  resume.translatesAutoresizingMaskIntoConstraints = NO;
  [resume addTarget:self action:@selector(resumeGameTapped:) forControlEvents:UIControlEventTouchUpInside];
  [panel addSubview:resume];

  UIButtonConfiguration* settings_config = [UIButtonConfiguration tintedButtonConfiguration];
  settings_config.title = @"Settings";
  settings_config.image = [UIImage systemImageNamed:@"slider.horizontal.3"];
  settings_config.imagePadding = 6;
  settings_config.baseForegroundColor = [XeniaTheme textPrimary];
  settings_config.baseBackgroundColor = [XeniaTheme bgSurface2];
  settings_config.cornerStyle = UIButtonConfigurationCornerStyleLarge;
  settings_config.contentInsets = NSDirectionalEdgeInsetsMake(10, 16, 10, 16);
  UIButton* settings = [UIButton buttonWithConfiguration:settings_config primaryAction:nil];
  settings.translatesAutoresizingMaskIntoConstraints = NO;
  [settings addTarget:self
               action:@selector(inGameSettingsTapped:)
     forControlEvents:UIControlEventTouchUpInside];
  [panel addSubview:settings];

  UIButtonConfiguration* live_log_config = [UIButtonConfiguration tintedButtonConfiguration];
  live_log_config.title = @"Live Log";
  live_log_config.image = [UIImage systemImageNamed:@"doc.text"];
  live_log_config.imagePadding = 6;
  live_log_config.baseForegroundColor = [XeniaTheme textPrimary];
  live_log_config.baseBackgroundColor = [XeniaTheme bgSurface2];
  live_log_config.cornerStyle = UIButtonConfigurationCornerStyleLarge;
  live_log_config.contentInsets = NSDirectionalEdgeInsetsMake(10, 16, 10, 16);
  UIButton* live_log = [UIButton buttonWithConfiguration:live_log_config primaryAction:nil];
  live_log.translatesAutoresizingMaskIntoConstraints = NO;
  [live_log addTarget:self
               action:@selector(inGameLiveLogTapped:)
     forControlEvents:UIControlEventTouchUpInside];
  [panel addSubview:live_log];

  UIButtonConfiguration* exit_config = [UIButtonConfiguration tintedButtonConfiguration];
  exit_config.title = @"Exit To Library";
  exit_config.image = [UIImage systemImageNamed:@"rectangle.portrait.and.arrow.right"];
  exit_config.imagePadding = 6;
  exit_config.baseForegroundColor = [XeniaTheme textPrimary];
  exit_config.baseBackgroundColor = [[XeniaTheme statusError] colorWithAlphaComponent:0.25];
  exit_config.cornerStyle = UIButtonConfigurationCornerStyleLarge;
  exit_config.contentInsets = NSDirectionalEdgeInsetsMake(10, 16, 10, 16);
  UIButton* exit_button = [UIButton buttonWithConfiguration:exit_config primaryAction:nil];
  exit_button.translatesAutoresizingMaskIntoConstraints = NO;
  [exit_button addTarget:self action:@selector(exitGameTapped:) forControlEvents:UIControlEventTouchUpInside];
  [panel addSubview:exit_button];

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

    [resume.topAnchor constraintEqualToAnchor:subtitle.bottomAnchor constant:16],
    [resume.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor constant:14],
    [resume.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor constant:-14],

    [settings.topAnchor constraintEqualToAnchor:resume.bottomAnchor constant:10],
    [settings.leadingAnchor constraintEqualToAnchor:resume.leadingAnchor],
    [settings.trailingAnchor constraintEqualToAnchor:resume.trailingAnchor],

    [live_log.topAnchor constraintEqualToAnchor:settings.bottomAnchor constant:10],
    [live_log.leadingAnchor constraintEqualToAnchor:resume.leadingAnchor],
    [live_log.trailingAnchor constraintEqualToAnchor:resume.trailingAnchor],

    [exit_button.topAnchor constraintEqualToAnchor:live_log.bottomAnchor constant:10],
    [exit_button.leadingAnchor constraintEqualToAnchor:resume.leadingAnchor],
    [exit_button.trailingAnchor constraintEqualToAnchor:resume.trailingAnchor],
    [exit_button.bottomAnchor constraintEqualToAnchor:panel.bottomAnchor constant:-14],
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
    self.inGameMenuOverlay.alpha = 0.0;
    self.inGameMenuOverlay.hidden = NO;
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
  BOOL requested_stop = self.appContext ? self.appContext->TerminateCurrentGame() : NO;
  if (requested_stop) {
    self.gameStopInProgress = YES;
    self.launcherOverlay.hidden = NO;
    self.launcherOverlay.alpha = 1.0;
    xe_request_portrait_orientation(self);
    self.statusLabel.text = @"Stopping game...";
  } else {
    self.statusLabel.text = @"No active game to stop.";
  }
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

  auto profiles = self.appContext->ListProfiles();
  UIAlertController* sheet =
      [UIAlertController alertControllerWithTitle:@"Profiles"
                                          message:@"Create a profile or sign in."
                                   preferredStyle:UIAlertControllerStyleActionSheet];

  [sheet addAction:[UIAlertAction actionWithTitle:@"Create Profile"
                                            style:UIAlertActionStyleDefault
                                          handler:^(__unused UIAlertAction* action) {
                                            [self presentProfileCreateAlert];
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
                                                return;
                                              }
                                              if (self.appContext->SignInProfile(xuid)) {
                                                [self refreshSignedInProfileUI];
                                                self.statusLabel.text = [NSString
                                                    stringWithFormat:@"Signed in as %@.", gamertag];
                                              }
                                            }]];
  }

  [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                            style:UIAlertActionStyleCancel
                                          handler:nil]];

  UIPopoverPresentationController* popover = sheet.popoverPresentationController;
  if (popover) {
    popover.sourceView = sender ?: self.profileButton;
    popover.sourceRect = (sender ?: self.profileButton).bounds;
  }

  [self presentViewController:sheet animated:YES completion:nil];
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
  BOOL jit_ready = self.jitAcquired;
  self.jitWarningCard.hidden = jit_ready;
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
    if (IsLikelyGodPath(p)) return 0;
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
                  IsLikelyGodPath(entry.path()))) {
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
              game.title = std::string([cached UTF8String]);
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

  std::sort(discovered_games_.begin(), discovered_games_.end(),
            [](const IOSDiscoveredGame& a, const IOSDiscoveredGame& b) {
              if (a.title == b.title) {
                return a.path.filename().string() < b.path.filename().string();
              }
              return a.title < b.title;
            });

  [self.importedGamesCollectionView reloadData];
  self.importedGamesEmptyLabel.hidden = !discovered_games_.empty();
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

- (void)launchGameAtPath:(const std::filesystem::path&)game_path
             displayName:(NSString*)display_name {
  if (self.gameStopInProgress || self.gameRunning) {
    self.statusLabel.text = @"Please wait for the current game to stop.";
    return;
  }
  if (!self.jitAcquired) {
    [self presentJITRequiredAlert];
    return;
  }

  NSString* path_ns = ToNSString(game_path.string());
  NSString* fallback_name = ToNSString(game_path.filename().string());
  NSString* game_label = display_name.length ? display_name : fallback_name;
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
    return cell;
  }

  const IOSDiscoveredGame& game = discovered_games_[static_cast<size_t>(indexPath.item)];
  NSString* title =
      game.title.empty() ? ToNSString(game.path.stem().string()) : ToNSString(game.title);
  cell.titleLabel.text = title;

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
  const IOSDiscoveredGame& game = discovered_games_[static_cast<size_t>(indexPath.item)];
  [self launchGameAtPath:game.path displayName:ToNSString(game.title)];
}

#pragma mark - UICollectionViewDelegateFlowLayout

- (CGSize)collectionView:(UICollectionView*)collectionView
                    layout:(UICollectionViewLayout* __unused)collectionViewLayout
    sizeForItemAtIndexPath:(NSIndexPath* __unused)indexPath {
  CGFloat content_width = collectionView.bounds.size.width;
  NSInteger columns = 2;
  if (content_width >= 1100) {
    columns = 5;
  } else if (content_width >= 900) {
    columns = 4;
  } else if (content_width >= 680) {
    columns = 3;
  }
  CGFloat spacing = 16.0;
  CGFloat total_spacing = spacing * (columns - 1);
  CGFloat tile_width = floor((content_width - total_spacing) / columns);
  tile_width = MAX(tile_width, 100.0);
  // Cover art is ~219x300 (~1:1.37).  Image height + 24pt for title label.
  CGFloat image_height = floor(tile_width * 300.0 / 219.0);
  return CGSizeMake(tile_width, image_height + 24.0);
}

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
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

  NSError* import_error = nil;
  std::filesystem::path imported_path = [self importGameIntoLibrary:url error:&import_error];
  if (access_granted) {
    [url stopAccessingSecurityScopedResource];
  }

  if (imported_path.empty()) {
    NSString* message = import_error.localizedDescription ?: @"Failed to import selected game.";
    UIAlertController* alert =
        [UIAlertController alertControllerWithTitle:@"Import Failed"
                                            message:message
                                     preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK"
                                              style:UIAlertActionStyleCancel
                                            handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
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
  xe_request_portrait_orientation(self);
  [UIView animateWithDuration:0.3
                   animations:^{
                     self.launcherOverlay.alpha = 1.0;
                   }];
}

- (void)dealloc {
  [self.jitPollTimer invalidate];
}

@end

// ---------------------------------------------------------------------------
// XeniaAppDelegate - UIKit application lifecycle.
// ---------------------------------------------------------------------------
@interface XeniaAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation XeniaAppDelegate {
  std::unique_ptr<xe::ui::IOSWindowedAppContext> app_context_;
  std::unique_ptr<xe::ui::WindowedApp> app_;
}

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
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
  self.window = [[UIWindow alloc]
      initWithFrame:[[UIScreen mainScreen] bounds]];
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

  XELOGI("iOS: Application launched successfully");
  return YES;
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
