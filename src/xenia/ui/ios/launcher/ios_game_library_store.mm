/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#import "xenia/ui/ios/launcher/ios_game_library_store.h"

#include <map>
#include <set>
#include <string>
#include <system_error>

#include "xenia/ui/ios/launcher/ios_compat_data.h"
#include "xenia/ui/ios/launcher/ios_content_management.h"
#include "xenia/ui/ios/shared/ios_system_utils.h"
#include "xenia/ui/ios/shared/ios_view_helpers.h"

namespace xe {
namespace ui {

namespace {

int LibraryFormatPriority(const std::filesystem::path& path) {
  if (IsLikelyGodContainerFile(path)) {
    return 0;
  }
  if (IsISOPath(path)) {
    return 1;
  }
  return 2;
}

}  // namespace

std::filesystem::path IOSImportedGamesDirectory() {
  return xe_get_ios_documents_path() / "games";
}

std::filesystem::path ImportGameIntoIOSLibrary(NSURL* source_url, NSError** error) {
  std::filesystem::path source_path([source_url.path UTF8String]);
  std::filesystem::path library_path = IOSImportedGamesDirectory();

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

std::vector<IOSDiscoveredGame> ScanIOSGameLibrary(NSDictionary* title_name_cache) {
  std::vector<IOSDiscoveredGame> games;

  std::vector<std::filesystem::path> scan_roots;
  const std::filesystem::path documents_root = xe_get_ios_documents_path();
  const std::filesystem::path library_root = IOSImportedGamesDirectory();
  scan_roots.push_back(library_root);
  if (documents_root != library_root) {
    scan_roots.push_back(documents_root);
  }

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
            NSString* key = XEFormatTitleIDHexLower(game.title_id);
            NSString* cached = [title_name_cache objectForKey:key];
            if (cached.length > 0) {
              game.title = NormalizeGameTitleForUI(std::string([cached UTF8String]));
            }
          }
          if (game.title_id) {
            auto existing = title_id_to_index.find(game.title_id);
            if (existing != title_id_to_index.end()) {
              IOSDiscoveredGame& existing_game = games[existing->second];
              int old_priority = LibraryFormatPriority(existing_game.path);
              int new_priority = LibraryFormatPriority(unique_path);
              if (new_priority < old_priority) {
                IOSDiscoveredGame previous_game = std::move(existing_game);
                existing_game = std::move(game);
                MergeDiscoveredGameDisc(&existing_game, previous_game);
              } else {
                MergeDiscoveredGameDisc(&existing_game, game);
              }
              ++it;
              continue;
            }
            title_id_to_index[game.title_id] = games.size();
          }
          games.push_back(std::move(game));
        }
      }

      ++it;
    }
  }

  for (auto& game : games) {
    if (!game.title_id) {
      continue;
    }
    std::error_code ec;
    if (std::filesystem::exists(xe_title_content_root(game.title_id), ec)) {
      game.has_installed_content = true;
    }
  }

  SortDiscoveredGames(&games);
  return games;
}

void ApplyIOSCompatibilityDataToDiscoveredGames(
    NSDictionary* compat_data, std::vector<IOSDiscoveredGame>* games) {
  if (!games) {
    return;
  }
  for (auto& game : *games) {
    game.has_compat_info = false;
    game.compat_status.clear();
    game.compat_perf.clear();
    game.compat_notes.clear();
    if (!compat_data || !game.title_id) {
      continue;
    }
    NSString* key = XEFormatTitleIDHexUpper(game.title_id);
    NSDictionary* info = [compat_data objectForKey:key];
    if (!info) {
      continue;
    }
    NSString* title = xe_string_from_object(info[@"title"]);
    if (title.length > 0) {
      game.title = NormalizeGameTitleForUI(std::string([title UTF8String]));
    }
    NSDictionary* summary = xe_preferred_summary_from_compat_info(info);
    NSDictionary* source = summary ?: info;
    NSString* status = xe_string_from_object(source[@"status"]);
    NSString* perf = xe_string_from_object(source[@"perf"]);
    NSString* notes = xe_string_from_object(source[@"notes"]);
    if ([status isKindOfClass:[NSString class]] && status.length > 0) {
      game.has_compat_info = true;
      game.compat_status = std::string([status UTF8String]);
      game.compat_perf =
          [perf isKindOfClass:[NSString class]] ? std::string([perf UTF8String]) : "";
      game.compat_notes =
          [notes isKindOfClass:[NSString class]] ? std::string([notes UTF8String]) : "";
    }
  }
}

}  // namespace ui
}  // namespace xe
