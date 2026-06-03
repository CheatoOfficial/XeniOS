/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_IOS_GAME_LIBRARY_STORE_H_
#define XENIA_UI_IOS_GAME_LIBRARY_STORE_H_

#import <Foundation/Foundation.h>

#include <filesystem>
#include <vector>

#include "xenia/ui/ios/launcher/ios_game_library.h"

namespace xe {
namespace ui {

std::filesystem::path IOSImportedGamesDirectory();

std::filesystem::path ImportGameIntoIOSLibrary(NSURL* source_url, NSError** error);

std::vector<IOSDiscoveredGame> ScanIOSGameLibrary(NSDictionary* title_name_cache);

void ApplyIOSCompatibilityDataToDiscoveredGames(NSDictionary* compat_data,
                                                std::vector<IOSDiscoveredGame>* games);

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_IOS_GAME_LIBRARY_STORE_H_
