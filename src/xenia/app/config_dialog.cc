/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/config_dialog.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "third_party/fmt/include/fmt/format.h"
#include "third_party/imgui/imgui.h"
#include "third_party/tomlplusplus/toml.hpp"
#include "xenia/app/emulator_window.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/system.h"
#include "xenia/config.h"
#include "xenia/emulator.h"
#include "xenia/ui/file_picker.h"

#ifdef XE_PLATFORM_WIN32
#include <shellapi.h>
#include <windows.h>
#endif

namespace xe {
namespace app {

namespace {

// Helper function for fuzzy matching - checks if all tokens are found in the
// text
bool MatchesAllTokens(const std::string& text_lower,
                      const std::vector<std::string>& tokens) {
  for (const auto& token : tokens) {
    if (text_lower.find(token) == std::string::npos) {
      return false;
    }
  }
  return true;
}
// Known enum-like cvars with their valid options
const std::map<std::string, std::vector<std::string>>& GetKnownEnumOptions() {
  static const std::map<std::string, std::vector<std::string>> options = {
#if XE_PLATFORM_WIN32
      {"gpu", {"any", "d3d12", "vulkan", "null"}},
      {"apu", {"any", "nop", "sdl", "xaudio2"}},
      {"hid", {"any", "nop", "sdl", "winkey", "xinput"}},
#elif XE_PLATFORM_LINUX
      {"gpu", {"any", "vulkan", "null"}},
      {"apu", {"any", "alsa", "nop", "sdl"}},
      {"hid", {"any", "nop", "sdl"}},
#else
      {"gpu", {"any", "vulkan", "null"}},
      {"apu", {"any", "nop", "sdl"}},
      {"hid", {"any", "nop", "sdl"}},
#endif
      {"d3d12_readback_resolve",
       {"kCopy", "kComputeLuminance", "kComputeRGBA16"}},
      {"render_target_path_d3d12", {"any", "rtv", "rov"}},
      {"render_target_path_vulkan", {"any", "fbo", "fsi"}},
      {"postprocess_antialiasing", {"off", "fxaa", "fxaa_extreme"}},
      {"postprocess_scaling_and_sharpening",
       {"bilinear", "cas", "fsr", "nearest"}},
  };
  return options;
}
}  // namespace

ConfigDialog::ConfigDialog(ui::ImGuiDrawer* imgui_drawer,
                           EmulatorWindow* emulator_window)
    : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {
  LoadConfigValues();
}

void ConfigDialog::LoadConfigValues() {
  config_vars_.clear();
  categories_.clear();
  has_unsaved_changes_ = false;  // Clear unsaved changes flag

  if (!cvar::ConfigVars) {
    XELOGW("ConfigVars not initialized");
    return;
  }

  // Collect all config variables that are actually saved to config.toml
  // Skip transient variables as they don't belong in the config file
  for (const auto& [name, var] : *cvar::ConfigVars) {
    if (var->is_transient()) {
      continue;  // Skip transient variables entirely
    }

    // Skip the [Config] category - it contains internal variables like
    // defaults_date
    if (var->category() == "Config") {
      continue;
    }

    ConfigVarInfo info;
    info.var = var;
    info.name = var->name();
    info.description = var->description();
    info.category = var->category();

    // Get the actual value without TOML formatting
    // Check if it's a string type to get the clean value
    if (auto* string_var = dynamic_cast<cvar::ConfigVar<std::string>*>(var)) {
      info.current_value = string_var->GetTypedConfigValue();
    } else if (auto* path_var =
                   dynamic_cast<cvar::ConfigVar<std::filesystem::path>*>(var)) {
      info.current_value = xe::path_to_utf8(path_var->GetTypedConfigValue());
    } else {
      // For non-string types (bool, int, double), use config_value()
      info.current_value = var->config_value();
    }

    info.pending_value = info.current_value;
    info.is_modified = false;
    info.is_transient = false;  // Always false now since we skip transient ones

    config_vars_.push_back(std::move(info));
  }

  // Organize by category
  for (auto& var_info : config_vars_) {
    auto& category = categories_[var_info.category];
    category.name = var_info.category;
    category.vars.push_back(&var_info);
  }

  // Sort variables within each category
  for (auto& [category_name, category] : categories_) {
    std::sort(category.vars.begin(), category.vars.end(),
              [](const ConfigVarInfo* a, const ConfigVarInfo* b) {
                return a->name < b->name;
              });
  }
}

void ConfigDialog::SaveConfigChanges() {
  XELOGI("Saving config changes");

  bool any_changed = false;
  for (auto& var_info : config_vars_) {
    if (var_info.is_modified) {
      // Parse and apply the new value based on type
      try {
        auto* var = var_info.var;

        // Create a temporary toml table to parse the value
        toml::table temp_table;
        std::string trimmed_value = var_info.pending_value;

        // Trim whitespace
        trimmed_value.erase(0, trimmed_value.find_first_not_of(" \t\n\r"));
        trimmed_value.erase(trimmed_value.find_last_not_of(" \t\n\r") + 1);

        // Check the actual type of the ConfigVar and parse accordingly
        if (dynamic_cast<cvar::ConfigVar<bool>*>(var)) {
          // Boolean
          temp_table.insert(var->name(), trimmed_value == "true");
        } else if (dynamic_cast<cvar::ConfigVar<std::string>*>(var)) {
          // String - use as-is, no quotes needed
          temp_table.insert(var->name(), trimmed_value);
        } else if (dynamic_cast<cvar::ConfigVar<std::filesystem::path>*>(var)) {
          // Path - convert to path
          temp_table.insert(var->name(), trimmed_value);
        } else if (dynamic_cast<cvar::ConfigVar<int32_t>*>(var) ||
                   dynamic_cast<cvar::ConfigVar<uint32_t>*>(var) ||
                   dynamic_cast<cvar::ConfigVar<int64_t>*>(var) ||
                   dynamic_cast<cvar::ConfigVar<uint64_t>*>(var)) {
          // Integer types
          try {
            if (trimmed_value.size() > 2 && trimmed_value[0] == '0' &&
                (trimmed_value[1] == 'x' || trimmed_value[1] == 'X')) {
              // Hex value
              int64_t val = std::stoll(trimmed_value, nullptr, 16);
              temp_table.insert(var->name(), val);
            } else {
              // Decimal value
              int64_t val = std::stoll(trimmed_value);
              temp_table.insert(var->name(), val);
            }
          } catch (...) {
            XELOGE("Failed to parse integer value for {}: {}", var_info.name,
                   trimmed_value);
            continue;
          }
        } else if (dynamic_cast<cvar::ConfigVar<float>*>(var) ||
                   dynamic_cast<cvar::ConfigVar<double>*>(var)) {
          // Float/double types
          try {
            double val = std::stod(trimmed_value);
            temp_table.insert(var->name(), val);
          } catch (...) {
            XELOGE("Failed to parse float value for {}: {}", var_info.name,
                   trimmed_value);
            continue;
          }
        } else {
          // Unknown type, try to parse generically
          XELOGW("Unknown config var type for {}, attempting generic parse",
                 var_info.name);
          temp_table.insert(var->name(), trimmed_value);
        }

        auto* node = temp_table.get(var->name());
        if (node) {
          var->LoadConfigValue(node);
          // Get the updated value properly based on type
          if (auto* string_var =
                  dynamic_cast<cvar::ConfigVar<std::string>*>(var)) {
            var_info.current_value = string_var->GetTypedConfigValue();
          } else if (auto* path_var =
                         dynamic_cast<cvar::ConfigVar<std::filesystem::path>*>(
                             var)) {
            var_info.current_value =
                xe::path_to_utf8(path_var->GetTypedConfigValue());
          } else {
            var_info.current_value = var->config_value();
          }
          var_info.pending_value = var_info.current_value;
          var_info.is_modified = false;
          any_changed = true;
        }

      } catch (const std::exception& e) {
        XELOGE("Failed to apply config value for {}: {}", var_info.name,
               e.what());
      }
    }
  }

  if (any_changed) {
    // Save the config file
    config::SaveConfig();
    has_unsaved_changes_ = false;
    XELOGI("Config saved successfully");

    // Refresh to show the actual saved values
    LoadConfigValues();
  }
}

void ConfigDialog::ResetToDefaults() {
  XELOGI("Resetting config to defaults (UI only)");

  for (auto& var_info : config_vars_) {
    // Get the default value without actually resetting the config variable
    var_info.var->ResetConfigValueToDefault();
    std::string default_value;

    // Get the default value properly based on type
    if (auto* string_var =
            dynamic_cast<cvar::ConfigVar<std::string>*>(var_info.var)) {
      default_value = string_var->GetTypedConfigValue();
    } else if (auto* path_var =
                   dynamic_cast<cvar::ConfigVar<std::filesystem::path>*>(
                       var_info.var)) {
      default_value = xe::path_to_utf8(path_var->GetTypedConfigValue());
    } else {
      default_value = var_info.var->config_value();
    }

    // Set the pending value to the default value and mark as modified if it
    // differs from current
    var_info.pending_value = default_value;
    var_info.is_modified = (var_info.pending_value != var_info.current_value);

    // Restore the actual config variable to its original value
    // This ensures the config isn't actually changed until the user saves
    try {
      toml::table temp_table;

      // Parse the current value back to restore it
      if (dynamic_cast<cvar::ConfigVar<bool>*>(var_info.var)) {
        temp_table.insert(var_info.var->name(),
                          var_info.current_value == "true");
      } else if (dynamic_cast<cvar::ConfigVar<std::string>*>(var_info.var) ||
                 dynamic_cast<cvar::ConfigVar<std::filesystem::path>*>(
                     var_info.var)) {
        temp_table.insert(var_info.var->name(), var_info.current_value);
      } else if (dynamic_cast<cvar::ConfigVar<int32_t>*>(var_info.var) ||
                 dynamic_cast<cvar::ConfigVar<uint32_t>*>(var_info.var) ||
                 dynamic_cast<cvar::ConfigVar<int64_t>*>(var_info.var) ||
                 dynamic_cast<cvar::ConfigVar<uint64_t>*>(var_info.var)) {
        // Integer types
        std::string trimmed = var_info.current_value;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
        trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);
        if (trimmed.size() > 2 && trimmed[0] == '0' &&
            (trimmed[1] == 'x' || trimmed[1] == 'X')) {
          int64_t val = std::stoll(trimmed, nullptr, 16);
          temp_table.insert(var_info.var->name(), val);
        } else {
          int64_t val = std::stoll(trimmed);
          temp_table.insert(var_info.var->name(), val);
        }
      } else if (dynamic_cast<cvar::ConfigVar<float>*>(var_info.var) ||
                 dynamic_cast<cvar::ConfigVar<double>*>(var_info.var)) {
        double val = std::stod(var_info.current_value);
        temp_table.insert(var_info.var->name(), val);
      } else {
        temp_table.insert(var_info.var->name(), var_info.current_value);
      }

      auto* node = temp_table.get(var_info.var->name());
      if (node) {
        var_info.var->LoadConfigValue(node);
      }
    } catch (...) {
      // If we can't restore, it's not critical since we didn't save yet
    }
  }

  // Update the unsaved changes flag based on whether any values are modified
  has_unsaved_changes_ =
      std::any_of(config_vars_.begin(), config_vars_.end(),
                  [](const ConfigVarInfo& v) { return v.is_modified; });
}

void ConfigDialog::OnDraw(ImGuiIO& io) {
  if (!emulator_window_->emulator()) {
    ImGui::Text("Emulator not initialized");
    return;
  }

  // Set window size
  ImVec2 window_size = ImVec2(900, 700);
  ImGui::SetNextWindowSize(window_size, ImGuiCond_FirstUseEver);

  // Center the window
  ImVec2 viewport_size = ImGui::GetMainViewport()->Size;
  ImVec2 window_pos = ImVec2((viewport_size.x - window_size.x) * 0.5f,
                             (viewport_size.y - window_size.y) * 0.5f);
  ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);

  bool window_open = true;
  if (!ImGui::Begin("Configuration Manager", &window_open,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove)) {
    ImGui::End();
    return;
  }

  if (!window_open) {
    if (has_unsaved_changes_) {
      // TODO: Show confirmation dialog
      SaveConfigChanges();
    }
    ImGui::End();
    Close();
    return;
  }

  ImGui::TextWrapped(
      "Manage emulator configuration settings. Changes will be saved to "
      "config.toml. "
      "Some settings may require a restart to take effect.");
  ImGui::Separator();

  // Filter input
  if (filter_text_.size() < 256) {
    filter_text_.resize(256);
  }

  // Set keyboard focus to the search bar when the dialog first appears
  if (ImGui::IsWindowAppearing()) {
    ImGui::SetKeyboardFocusHere();
  }

  ImGui::InputTextWithHint("##config_filter",
                           "Filter by name or description...",
                           filter_text_.data(), filter_text_.size());

  ImGui::SameLine();
  ImGui::Checkbox("Modified only", &filter_modified_only_);
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Show only variables with unsaved changes");
  }

  // Action buttons
  if (ImGui::Button("Save Changes")) {
    SaveConfigChanges();
  }

  ImGui::SameLine();
  if (ImGui::Button("Discard Changes")) {
    LoadConfigValues();
  }

  ImGui::SameLine();
  if (ImGui::Button("Reset to Defaults")) {
    ResetToDefaults();
  }

  if (has_unsaved_changes_) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Unsaved changes");
  }

  ImGui::Separator();

  // Config variables list
  if (config_vars_.empty()) {
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                       "No configuration variables found");
  } else {
    // Get filter text as lowercase for case-insensitive search
    std::string filter_str(filter_text_.data());
    std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Split filter into tokens by whitespace
    std::vector<std::string> filter_tokens;
    if (!filter_str.empty()) {
      std::istringstream iss(filter_str);
      std::string token;
      while (iss >> token) {
        if (!token.empty()) {
          filter_tokens.push_back(token);
        }
      }
    }

    // Display categories and their variables
    for (auto& [category_name, category] : categories_) {
      // Count visible variables in this category
      int visible_count = 0;
      for (const auto* var_info : category.vars) {
        // Filter by modified status
        if (filter_modified_only_ && !var_info->is_modified) {
          continue;
        }

        // Filter by text tokens
        if (!filter_tokens.empty()) {
          std::string name_lower = var_info->name;
          std::string desc_lower = var_info->description;
          std::transform(name_lower.begin(), name_lower.end(),
                         name_lower.begin(),
                         [](unsigned char c) { return std::tolower(c); });
          std::transform(desc_lower.begin(), desc_lower.end(),
                         desc_lower.begin(),
                         [](unsigned char c) { return std::tolower(c); });

          // Check if all tokens match either name or description
          std::string combined = name_lower + " " + desc_lower;
          if (!MatchesAllTokens(combined, filter_tokens)) {
            continue;
          }
        }
        visible_count++;
      }

      if (visible_count == 0) continue;

      // Category header
      ImGui::PushID(category_name.c_str());

      bool expanded = ImGui::CollapsingHeader(
          fmt::format("{} ({} settings)", category_name, visible_count).c_str(),
          ImGuiTreeNodeFlags_DefaultOpen);

      if (expanded) {
        ImGui::Indent();

        // Display each config variable
        for (auto* var_info : category.vars) {
          // Apply modified filter
          if (filter_modified_only_ && !var_info->is_modified) {
            continue;
          }

          // Apply text filter with tokens
          if (!filter_tokens.empty()) {
            std::string name_lower = var_info->name;
            std::string desc_lower = var_info->description;
            std::transform(name_lower.begin(), name_lower.end(),
                           name_lower.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            std::transform(desc_lower.begin(), desc_lower.end(),
                           desc_lower.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            // Check if all tokens match either name or description
            std::string combined = name_lower + " " + desc_lower;
            if (!MatchesAllTokens(combined, filter_tokens)) {
              continue;
            }
          }

          ImGui::PushID(var_info->name.c_str());

          // Use columns for better layout
          // Column 1: Variable name (fixed width, with ellipsis for long names)
          ImGui::Text("%.40s%s", var_info->name.c_str(),
                      var_info->name.length() > 40 ? "..." : "");

          // Show full name and description as tooltip when hovering over the
          // variable name
          if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() *
                                   35.0f);  // Wrap at ~35 characters width
            if (!var_info->description.empty()) {
              ImGui::Text("%s", var_info->name.c_str());
              ImGui::Separator();
              ImGui::TextWrapped("%s", var_info->description.c_str());
            } else {
              ImGui::Text("%s", var_info->name.c_str());
            }
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
          }

          // Column 2: Input field for value
          ImGui::SameLine();
          ImGui::SetCursorPosX(
              450);  // Move even further right for long names with ...

          // Make input field width consistent
          ImGui::SetNextItemWidth(300);  // Adjust width to fit in window

          // Detect type from current value to show appropriate control
          bool value_changed = false;
          std::string trimmed_current = var_info->current_value;
          trimmed_current.erase(0,
                                trimmed_current.find_first_not_of(" \t\n\r"));
          trimmed_current.erase(trimmed_current.find_last_not_of(" \t\n\r") +
                                1);

          // Check if this is an enum-like cvar with known options
          const auto& enum_options = GetKnownEnumOptions();
          auto enum_it = enum_options.find(var_info->name);

          if (trimmed_current == "true" || trimmed_current == "false") {
            // Boolean checkbox
            bool bool_val = (var_info->pending_value == "true");
            if (ImGui::Checkbox("##value", &bool_val)) {
              var_info->pending_value = bool_val ? "true" : "false";
              value_changed = true;
            }
          } else if (enum_it != enum_options.end()) {
            // Dropdown for enum-like cvars
            const auto& options = enum_it->second;
            int current_index = -1;

            // Find current value in options
            for (size_t i = 0; i < options.size(); ++i) {
              if (options[i] == var_info->pending_value) {
                current_index = static_cast<int>(i);
                break;
              }
            }

            // If current value not found, add it to show it's custom
            std::vector<const char*> combo_items;
            for (const auto& option : options) {
              combo_items.push_back(option.c_str());
            }

            if (current_index == -1) {
              // Current value is not in the list, show it as custom
              combo_items.push_back(var_info->pending_value.c_str());
              current_index = static_cast<int>(combo_items.size() - 1);
            }

            if (ImGui::Combo("##value", &current_index, combo_items.data(),
                             static_cast<int>(combo_items.size()))) {
              if (current_index >= 0 &&
                  current_index < static_cast<int>(options.size())) {
                var_info->pending_value = options[current_index];
                value_changed = true;
              }
            }
          } else if (dynamic_cast<cvar::ConfigVar<std::filesystem::path>*>(
                         var_info->var)) {
            // Define path configuration mappings
            struct PathConfig {
              ui::FilePicker::Type type;
              std::vector<std::pair<std::string, std::string>> extensions;
            };

            static const std::unordered_map<std::string, PathConfig>
                path_configs = {
                    // File pickers
                    {"trace_function_data_path",
                     {ui::FilePicker::Type::kFile, {}}},
                    {"dump_shaders", {ui::FilePicker::Type::kFile, {}}},
                    {"target_trace_file", {ui::FilePicker::Type::kFile, {}}},
                    {"trace_dump_path", {ui::FilePicker::Type::kFile, {}}},
                    {"custom_font_path", {ui::FilePicker::Type::kFile, {}}},
                    {"log_file", {ui::FilePicker::Type::kFile, {}}},

                    // Directory pickers
                    {"content_root", {ui::FilePicker::Type::kDirectory, {}}},
                    {"cache_root", {ui::FilePicker::Type::kDirectory, {}}},
                };

            // Check if this path should have a browse button
            auto config_it = path_configs.find(var_info->name);
            bool has_browse_button = (config_it != path_configs.end());

            // Path input with optional browse button
            char value_buffer[1024];
            std::strncpy(value_buffer, var_info->pending_value.c_str(),
                         sizeof(value_buffer) - 1);
            value_buffer[sizeof(value_buffer) - 1] = '\0';

            // Make the input field narrower if we have a browse button
            if (has_browse_button) {
              ImGui::SetNextItemWidth(250);
            }

            if (ImGui::InputText("##value", value_buffer,
                                 sizeof(value_buffer))) {
              var_info->pending_value = value_buffer;
              value_changed = true;
            }

            if (has_browse_button) {
              ImGui::SameLine();
              if (ImGui::Button("Browse...")) {
                auto file_picker = ui::FilePicker::Create();
                if (file_picker) {
                  const PathConfig& config = config_it->second;

                  file_picker->set_mode(ui::FilePicker::Mode::kOpen);
                  file_picker->set_type(config.type);
                  file_picker->set_title(fmt::format(
                      "Select {} for {}",
                      config.type == ui::FilePicker::Type::kFile ? "File"
                                                                 : "Directory",
                      var_info->name));

                  // Set initial directory
                  std::filesystem::path initial_dir;
                  if (!var_info->pending_value.empty()) {
                    std::filesystem::path current_path(var_info->pending_value);
                    if (std::filesystem::exists(current_path)) {
                      if (config.type == ui::FilePicker::Type::kFile) {
                        // For files, use the parent directory
                        initial_dir = current_path.parent_path();
                      } else {
                        // For directories, use the directory itself
                        initial_dir = current_path;
                      }
                    }
                  }

                  // Fall back to content_root's parent if initial_dir is empty
                  // or doesn't exist
                  if (initial_dir.empty() ||
                      !std::filesystem::exists(initial_dir)) {
                    initial_dir = emulator_window_->emulator()
                                      ->content_root()
                                      .parent_path();
                  }

                  file_picker->set_initial_directory(initial_dir);

                  if (!config.extensions.empty()) {
                    file_picker->set_extensions(config.extensions);
                  }

                  if (file_picker->Show(emulator_window_->window())) {
                    auto files = file_picker->selected_files();
                    if (!files.empty()) {
                      var_info->pending_value = xe::path_to_utf8(files[0]);
                      value_changed = true;
                    }
                  }
                }
              }
            }
          } else {
            // Text input for all other string types
            char value_buffer[1024];
            std::strncpy(value_buffer, var_info->pending_value.c_str(),
                         sizeof(value_buffer) - 1);
            value_buffer[sizeof(value_buffer) - 1] = '\0';

            if (ImGui::InputText("##value", value_buffer,
                                 sizeof(value_buffer))) {
              var_info->pending_value = value_buffer;
              value_changed = true;
            }
          }

          if (value_changed) {
            var_info->is_modified =
                (var_info->pending_value != var_info->current_value);
            has_unsaved_changes_ = std::any_of(
                config_vars_.begin(), config_vars_.end(),
                [](const ConfigVarInfo& v) { return v.is_modified; });
          }

          // Show modified indicator
          if (var_info->is_modified) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "*");
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("This value has been modified but not saved");
            }
          }

          ImGui::PopID();
        }

        ImGui::Unindent();
      }

      ImGui::PopID();
    }
  }

  ImGui::End();
}

}  // namespace app
}  // namespace xe
