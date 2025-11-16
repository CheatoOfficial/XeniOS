/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CONFIG_H_
#define XENIA_CONFIG_H_

#include <filesystem>
#include <functional>
#include "third_party/tomlplusplus/toml.hpp"

toml::parse_result ParseFile(const std::filesystem::path& filename);

namespace config {
extern std::filesystem::path config_folder;
extern std::string game_config_suffix;

void SetupConfig(const std::filesystem::path& config_folder);
std::vector<std::string> LoadGameConfigAsArgs(const std::string_view title_id);
toml::table LoadGameConfig(uint32_t title_id);
void SaveConfig();
void SaveGameConfig(uint32_t title_id, const toml::table& config_table);
void ReloadConfig();
void SetConfigSavedCallback(std::function<void()> callback);
}  // namespace config

#endif  // XENIA_CONFIG_H_
