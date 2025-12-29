/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "config.h"

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/base/string_buffer.h"
#include "xenia/base/system.h"
#include "xenia/emulator.h"
#include "xenia/ui/config_helpers.h"

toml::parse_result ParseFile(const std::filesystem::path& filename) {
  return toml::parse_file(xe::path_to_utf8(filename));
}

CmdVar(config, "", "Specifies the target config to load.");

DEFINE_uint32(
    defaults_date, 0,
    "Do not modify - internal version of the default values in the config, for "
    "seamless updates if default value of any option is changed.",
    "Config");

namespace config {
std::string config_name = "xenia-edge.config.toml";
std::filesystem::path config_folder;
std::filesystem::path config_path;
std::string game_config_suffix = ".config.toml";
std::function<void()> config_saved_callback;

bool sortCvar(cvar::IConfigVar* a, cvar::IConfigVar* b) {
  if (a->category() < b->category()) return true;
  if (a->category() > b->category()) return false;
  if (a->name() < b->name()) return true;
  return false;
}

toml::parse_result ParseConfig(const std::filesystem::path& config_path) {
  try {
    return ParseFile(config_path);
  } catch (toml::parse_error& e) {
    xe::FatalError(fmt::format("Failed to parse config file '{}':\n\n{}",
                               config_path, e.what()));
    return toml::parse_result();
  }
}

void PrintConfigToLog(const std::filesystem::path& file_path) {
  std::ifstream file(file_path);
  if (!file.is_open()) {
    return;
  }

  std::string config_dump = "----------- CONFIG DUMP -----------\n";
  std::string config_line = "";
  while (std::getline(file, config_line)) {
    if (config_line.empty()) {
      continue;
    }

    // Find place where comment begins and cut that part.
    const size_t comment_mark_position = config_line.find_first_of("#");
    if (comment_mark_position != std::string::npos) {
      config_line.erase(comment_mark_position, config_line.length());
    }

    // Check if remaining part of line is empty.
    if (std::all_of(config_line.cbegin(), config_line.cend(), isspace)) {
      continue;
    }
    // Check if line is a category mark. If it is add new line on start for
    // improved visibility.
    const bool category_mark = config_line.at(0) == '[';
    config_dump += (category_mark ? "\n" : "") + config_line + "\n";
  }
  config_dump += "----------- END OF CONFIG DUMP ----";
  XELOGI("{}", config_dump);

  file.close();
}

void MigrateLegacyCvars(const toml::table& config) {
  if (!cvar::ConfigVars) {
    return;
  }

  for (const auto& [category_name, category_table] : config) {
    if (!category_table.is_table()) continue;

    for (const auto& [key, value] : *category_table.as_table()) {
      std::string var_name = std::string(key);
      std::string var_value = value.value_or("");

      if (var_value.length() >= 2 && var_value.front() == '"' &&
          var_value.back() == '"') {
        var_value = var_value.substr(1, var_value.length() - 2);
      }

      for (const auto& alias : xe::ui::GetCvarAliases()) {
        // Support wildcard "*" to match any value and copy it as-is
        bool name_matches = (var_name == alias.old_name);
        bool value_matches =
            (alias.old_value == "*") || (var_value == alias.old_value);

        if (name_matches && value_matches) {
          auto new_cvar = (*cvar::ConfigVars).find(alias.new_name);
          if (new_cvar != (*cvar::ConfigVars).end()) {
            auto config_var = static_cast<cvar::IConfigVar*>(new_cvar->second);
            // If new_value is "*", copy the original value as-is
            std::string final_value =
                (alias.new_value == "*") ? var_value : alias.new_value;
            toml::value new_value(final_value);
            config_var->LoadConfigValue(&new_value);
          }
          break;
        }
      }
    }
  }
}

void ReadConfig(const std::filesystem::path& file_path,
                bool update_if_no_version_stored) {
  if (!cvar::ConfigVars) {
    return;
  }

  const auto config = ParseConfig(file_path);

  PrintConfigToLog(file_path);

  // Loading an actual global config file that exists - if there's no
  // defaults_date in it, it's very old (before updating was added at all, thus
  // all defaults need to be updated).
  auto defaults_date_cvar =
      dynamic_cast<cvar::ConfigVar<uint32_t>*>(cv::cv_defaults_date);
  assert_not_null(defaults_date_cvar);
  defaults_date_cvar->SetConfigValue(0);
  for (auto& it : *cvar::ConfigVars) {
    auto config_var = static_cast<cvar::IConfigVar*>(it.second);
    toml::path config_key =
        toml::path(config_var->category() + "." + config_var->name());

    const auto config_key_node = config.at_path(config_key);
    if (config_key_node) {
      config_var->LoadConfigValue(config_key_node.node());
    }
  }

  // Log command line overrides (after config values are loaded)
  bool has_overrides = false;
  for (const auto& it : *cvar::ConfigVars) {
    auto config_var = static_cast<cvar::IConfigVar*>(it.second);
    if (config_var->commandline_value() != config_var->config_value()) {
      if (!has_overrides) {
        XELOGI("Command line overrides:");
        has_overrides = true;
      }
      XELOGI("  {} = {}", config_var->name(), config_var->commandline_value());
    }
  }

  MigrateLegacyCvars(config);
  uint32_t config_defaults_date = defaults_date_cvar->GetTypedConfigValue();
  if (update_if_no_version_stored || config_defaults_date) {
    cvar::IConfigVarUpdate::ApplyUpdates(config_defaults_date);
  }

  // Check for type mismatch warnings
  if (cvar::config_type_mismatch_warnings &&
      !cvar::config_type_mismatch_warnings->empty()) {
    std::string warning_message =
        "The following config values had invalid types and have been reset to "
        "defaults:\n\n";
    for (const auto& name : *cvar::config_type_mismatch_warnings) {
      warning_message += "  - " + name + "\n";
    }
    warning_message +=
        "\nPlease check your config file. The config will be saved with the "
        "correct types.";

    xe::ShowSimpleMessageBox(xe::SimpleMessageBoxType::Warning,
                             warning_message);

    // Clear warnings
    cvar::config_type_mismatch_warnings->clear();
  }

  XELOGI("Loaded config: {}", file_path);
}

std::vector<std::string> LoadGameConfigAsArgs(const std::string_view title_id) {
  std::vector<std::string> args;

  const auto game_config_path =
      config_folder / "config" / (std::string(title_id) + game_config_suffix);

  if (!std::filesystem::exists(game_config_path)) {
    return args;
  }

  if (!cvar::ConfigVars) {
    return args;
  }

  try {
    const auto config = ParseConfig(game_config_path);

    // Iterate through all registered cvars
    for (auto& it : *cvar::ConfigVars) {
      auto config_var = static_cast<cvar::IConfigVar*>(it.second);
      toml::path config_key =
          toml::path(config_var->category() + "." + config_var->name());

      const auto config_key_node = config.at_path(config_key);
      if (config_key_node) {
        // Save the current config value state so we can restore it after
        // parsing This prevents game-specific overrides from contaminating the
        // parent process's global config when SaveConfig() is called later
        void* saved_state = config_var->SaveConfigValueState();

        // Load the value into the cvar temporarily to validate and convert it
        config_var->LoadConfigValue(config_key_node.node());

        // Get the string representation from the cvar
        std::string value = config_var->config_value();

        // Restore the original config value state
        config_var->RestoreConfigValueState(saved_state);

        // Strip TOML quotes from string values (they're added by ToString for
        // TOML format)
        if (value.length() >= 2 && value.front() == '"' &&
            value.back() == '"') {
          value = value.substr(1, value.length() - 2);
        }

        args.push_back(fmt::format("--{}={}", config_var->name(), value));
      }
    }

    XELOGI("Loaded per-game config for title {} with {} overrides", title_id,
           args.size());
  } catch (const std::exception& e) {
    XELOGE("Failed to parse per-game config {}: {}",
           xe::path_to_utf8(game_config_path), e.what());
  }

  return args;
}

void SaveGameConfig(uint32_t title_id, const toml::table& config_table) {
  const auto game_config_path =
      config_folder / "config" /
      (fmt::format("{:08X}", title_id) + game_config_suffix);

  try {
    xe::filesystem::CreateParentFolder(game_config_path);
    std::ofstream file(game_config_path);
    if (!file.is_open()) {
      throw std::runtime_error("Failed to open file for writing");
    }

    file << "# Game-specific config overrides\n";
    file << "# Title ID: " << fmt::format("{:08X}", title_id) << "\n\n";
    file << config_table << "\n";
    file.close();

    XELOGI("Saved game config for title {:08X}", title_id);
  } catch (const std::exception& e) {
    XELOGE("Failed to save game config {}: {}",
           xe::path_to_utf8(game_config_path), e.what());
    throw;
  }
}

void SaveGameConfigSetting(xe::Emulator* emulator, const char* section,
                           const char* cvar_name, const std::string& value) {
  if (!emulator || !emulator->is_title_open()) {
    return;
  }

  uint32_t title_id = emulator->title_id();
  toml::table config_table = LoadGameConfig(title_id);

  if (!config_table.contains(section)) {
    config_table.insert(section, toml::table{});
  }

  auto* section_table = config_table[section].as_table();
  if (section_table) {
    section_table->insert_or_assign(cvar_name, value);
  }

  SaveGameConfig(title_id, config_table);
}

void SaveGameConfigSetting(xe::Emulator* emulator, const char* section,
                           const char* cvar_name, bool value) {
  if (!emulator || !emulator->is_title_open()) {
    return;
  }

  uint32_t title_id = emulator->title_id();
  toml::table config_table = LoadGameConfig(title_id);

  if (!config_table.contains(section)) {
    config_table.insert(section, toml::table{});
  }

  auto* section_table = config_table[section].as_table();
  if (section_table) {
    section_table->insert_or_assign(cvar_name, value);
  }

  SaveGameConfig(title_id, config_table);
}

void SaveGameConfigSetting(xe::Emulator* emulator, const char* section,
                           const char* cvar_name, uint32_t value) {
  if (!emulator || !emulator->is_title_open()) {
    return;
  }

  uint32_t title_id = emulator->title_id();
  toml::table config_table = LoadGameConfig(title_id);

  if (!config_table.contains(section)) {
    config_table.insert(section, toml::table{});
  }

  auto* section_table = config_table[section].as_table();
  if (section_table) {
    section_table->insert_or_assign(cvar_name, value);
  }

  SaveGameConfig(title_id, config_table);
}

void SaveGameConfigSetting(xe::Emulator* emulator, const char* section,
                           const char* cvar_name, double value) {
  if (!emulator || !emulator->is_title_open()) {
    return;
  }

  uint32_t title_id = emulator->title_id();
  toml::table config_table = LoadGameConfig(title_id);

  if (!config_table.contains(section)) {
    config_table.insert(section, toml::table{});
  }

  auto* section_table = config_table[section].as_table();
  if (section_table) {
    section_table->insert_or_assign(cvar_name, value);
  }

  SaveGameConfig(title_id, config_table);
}

toml::table LoadGameConfig(uint32_t title_id) {
  const auto game_config_path =
      config_folder / "config" /
      (fmt::format("{:08X}", title_id) + game_config_suffix);

  toml::table config_table;
  if (std::filesystem::exists(game_config_path)) {
    try {
      config_table = toml::parse_file(game_config_path.string());
    } catch (const std::exception& e) {
      XELOGE("Failed to parse game config {}: {}",
             xe::path_to_utf8(game_config_path), e.what());
    }
  }
  return config_table;
}

void ReloadConfig() {
  if (config_path.empty()) {
    return;
  }

  if (std::filesystem::exists(config_path)) {
    ReadConfig(config_path, false);
    XELOGI("Reloaded config from: {}", xe::path_to_utf8(config_path));
  }
}

void SaveConfig() {
  if (config_path.empty()) {
    return;
  }

  // All cvar defaults have been updated on loading - store the current date.
  auto defaults_date_cvar =
      dynamic_cast<cvar::ConfigVar<uint32_t>*>(cv::cv_defaults_date);
  assert_not_null(defaults_date_cvar);
  defaults_date_cvar->SetConfigValue(
      cvar::IConfigVarUpdate::GetLastUpdateDate());

  std::vector<cvar::IConfigVar*> vars;
  if (cvar::ConfigVars) {
    for (const auto& s : *cvar::ConfigVars) {
      vars.push_back(s.second);
    }
  }
  std::sort(vars.begin(), vars.end(), [](auto a, auto b) {
    if (a->category() < b->category()) return true;
    if (a->category() > b->category()) return false;
    if (a->name() < b->name()) return true;
    return false;
  });

  // we use our own write logic because cpptoml doesn't
  // allow us to specify comments :(
  std::string last_category;
  bool last_multiline_description = false;
  xe::StringBuffer sb;
  for (auto config_var : vars) {
    if (config_var->is_transient()) {
      continue;
    }

    if (last_category != config_var->category()) {
      if (!last_category.empty()) {
        sb.Append('\n', 2);
      }
      last_category = config_var->category();
      last_multiline_description = false;
      sb.AppendFormat("[{}]\n", last_category);
    } else if (last_multiline_description) {
      last_multiline_description = false;
      sb.Append('\n');
    }

    auto value = config_var->config_value();
    size_t line_count;
    if (xe::utf8::find_any_of(value, "\n") == std::string_view::npos) {
      auto line = fmt::format("{} = {}", config_var->name(),
                              config_var->config_value());
      sb.Append(line);
      line_count = xe::utf8::count(line);
    } else {
      auto lines = xe::utf8::split(value, "\n");
      auto first = lines.cbegin();
      sb.AppendFormat("{} = {}\n", config_var->name(), *first);
      auto last = std::prev(lines.cend());
      for (auto it = std::next(first); it != last; ++it) {
        sb.Append(*it);
        sb.Append('\n');
      }
      sb.Append(*last);
      line_count = xe::utf8::count(*last);
    }

    constexpr size_t value_alignment = 50;
    const auto& description = config_var->description();
    if (!description.empty()) {
      if (line_count < value_alignment) {
        sb.Append(' ', value_alignment - line_count);
      }
      if (xe::utf8::find_any_of(description, "\n") == std::string_view::npos) {
        sb.AppendFormat("\t# {}\n", config_var->description());
      } else {
        auto lines = xe::utf8::split(description, "\n");
        auto first = lines.cbegin();
        sb.Append("\t# ");
        sb.Append(*first);
        sb.Append('\n');
        for (auto it = std::next(first); it != lines.cend(); ++it) {
          sb.Append(' ', value_alignment);
          sb.Append("\t# ");
          sb.Append(*it);
          sb.Append('\n');
        }
        last_multiline_description = true;
      }
    }
  }

  // save the config file
  xe::filesystem::CreateParentFolder(config_path);

  auto handle = xe::filesystem::OpenFile(config_path, "wb");
  if (!handle) {
    XELOGE("Failed to open '{}' for writing.", config_path);
  } else {
    fwrite(sb.buffer(), 1, sb.length(), handle);
    fclose(handle);
  }

  // Notify that config was saved
  if (config_saved_callback) {
    config_saved_callback();
  }
}

void SetConfigSavedCallback(std::function<void()> callback) {
  config_saved_callback = callback;
}

void SetupConfig(const std::filesystem::path& config_folder) {
  config::config_folder = config_folder;
  // check if the user specified a specific config to load
  if (!cvars::config.empty()) {
    config_path = xe::to_path(cvars::config);
    if (std::filesystem::exists(config_path)) {
      // An external config file may contain only explicit overrides - in this
      // case, it will likely not contain the defaults version; don't update
      // from the version 0 in this case. Or, it may be a full config - in this
      // case, if it's recent enough (created at least in 2021), it will contain
      // the version number - updates the defaults in it.
      ReadConfig(config_path, false);
      return;
    }
  }
  // if the user specified a --config argument, but the file doesn't exist,
  // let's also load the default config
  if (!config_folder.empty()) {
    config_path = config_folder / config_name;
    if (std::filesystem::exists(config_path)) {
      ReadConfig(config_path, true);
    }
    // Re-save the loaded config to present the most up-to-date list of
    // parameters to the user, if new options were added, descriptions were
    // updated, or default values were changed.
    SaveConfig();
  }
}

}  // namespace config
