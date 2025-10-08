/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/recent_titles_ui.h"
#include <chrono>
#include <thread>
#include "third_party/fmt/include/fmt/format.h"
#include "xenia/app/emulator_window.h"
#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"
#include "xenia/base/system.h"
#include "xenia/base/utf8.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/profile_manager.h"
#include "xenia/kernel/xam/user_tracker.h"
#include "xenia/kernel/xam/xam_state.h"
#include "xenia/ui/imgui_guest_notification.h"

namespace xe {
namespace app {

RecentTitlesUI::RecentTitlesUI(ui::ImGuiDrawer* imgui_drawer,
                               EmulatorWindow* emulator_window)
    : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {
  LoadRecentTitles();
}

RecentTitlesUI::~RecentTitlesUI() {
  for (auto& entry : title_icons_) {
    entry.second.release();
  }
}

void RecentTitlesUI::LoadRecentTitles() {
  recent_titles_.clear();
  selected_title_ = 0;  // Reset selection when reloading

  if (!emulator_window_) {
    return;
  }

  const auto& emulator_recent_titles =
      emulator_window_->GetRecentlyLaunchedTitles();

  for (const auto& entry : emulator_recent_titles) {
    recent_titles_.push_back(
        {entry.title_name, entry.path_to_file, entry.last_run_time, 0, {}});
  }
}

void RecentTitlesUI::TryLoadIcons() {
  if (!emulator_window_ || !emulator_window_->emulator()) {
    return;
  }

  auto kernel_state = emulator_window_->emulator()->kernel_state();
  if (!kernel_state) {
    return;
  }

  auto xam_state = kernel_state->xam_state();
  if (!xam_state) {
    return;
  }

  auto user_tracker = xam_state->user_tracker();
  auto profile_manager = xam_state->profile_manager();

  if (!user_tracker || !profile_manager) {
    return;
  }

  ui::IconsData icon_data;

  // Check all logged in profiles
  for (uint8_t user_index = 0; user_index < 4; user_index++) {
    const auto profile = profile_manager->GetProfile(user_index);
    if (!profile) {
      continue;
    }

    // Get all played titles for this profile
    auto played_titles = user_tracker->GetPlayedTitles(profile->xuid());

    // Match each recent title with played titles by name
    for (auto& recent_title : recent_titles_) {
      for (const auto& played_title : played_titles) {
        std::string played_name = xe::to_utf8(played_title.title_name);
        // Remove null terminator if present
        if (!played_name.empty() && played_name.back() == '\0') {
          played_name.pop_back();
        }
        std::string trimmed_played = xe::string_util::trim(played_name);
        std::string trimmed_recent =
            xe::string_util::trim(recent_title.title_name);

        if (trimmed_played == trimmed_recent) {
          if (!played_title.icon.empty()) {
            recent_title.icon = std::vector<uint8_t>(played_title.icon.begin(),
                                                     played_title.icon.end());
            // Update the title ID from the played title
            recent_title.title_id = played_title.id;
            icon_data[recent_title.title_id] = recent_title.icon;
          }
          break;  // Found match for this recent title
        }
      }
    }
  }

  if (!icon_data.empty() && imgui_drawer()) {
    // Load icons
    for (const auto& [title_id, icon_data_entry] : icon_data) {
      try {
        auto texture = imgui_drawer()->LoadImGuiIcon(icon_data_entry);
        if (texture) {
          title_icons_[title_id] = std::move(texture);
        }
      } catch (const std::exception& e) {
        XELOGE("Failed to load icon for title {}: {}", title_id, e.what());
        // Continue with other icons
      } catch (...) {
        XELOGE("Failed to load icon for title {}: unknown error", title_id);
        // Continue with other icons
      }
    }
  }
}

void RecentTitlesUI::RefreshIcons() {
  // Clear existing icons and force reload
  for (auto& entry : title_icons_) {
    entry.second.release();
  }
  title_icons_.clear();

  // Reset the title IDs to force re-matching
  for (auto& title : recent_titles_) {
    title.title_id = 0;
    title.icon.clear();
  }
}

void RecentTitlesUI::DrawTitleEntry(ImGuiIO& io, RecentTitleDisplay& entry,
                                    size_t index) {
  // First Column - Icon
  ImGui::TableSetColumnIndex(0);
  const auto start_position = ImGui::GetCursorPos();

  auto icon_it = title_icons_.find(entry.title_id);
  if (icon_it != title_icons_.end() && icon_it->second) {
    ImGui::Image(reinterpret_cast<ImTextureID>(icon_it->second.get()),
                 ui::default_image_icon_size);
  } else {
    if (!has_logged_in_profile_) {
      // Show "Not logged in" text when no profile is logged in
      ImVec2 pos = ImGui::GetCursorPos();

      // Create a child region to contain the text within icon bounds
      ImGui::BeginChild(fmt::format("##NoIcon{}", index).c_str(),
                        ui::default_image_icon_size, false,
                        ImGuiWindowFlags_NoScrollbar);

      // Draw each line centered individually
      const char* lines[] = {"Not", "logged", "in"};
      float line_height = ImGui::GetTextLineHeight();
      float total_height = line_height * 3;
      float start_y = (ui::default_image_icon_size.y - total_height) * 0.5f;

      ImGui::PushStyleColor(ImGuiCol_Text,
                            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

      for (int i = 0; i < 3; i++) {
        ImVec2 line_size = ImGui::CalcTextSize(lines[i]);
        float x_pos = (ui::default_image_icon_size.x - line_size.x) * 0.5f;
        float y_pos = start_y + (i * line_height);
        ImGui::SetCursorPos(ImVec2(x_pos, y_pos));
        ImGui::TextUnformatted(lines[i]);
      }

      ImGui::PopStyleColor();

      ImGui::EndChild();
    } else {
      // Just show empty space if logged in but no icon found
      ImGui::Dummy(ui::default_image_icon_size);
    }
  }

  // Second Column - Title Info
  ImGui::TableNextColumn();

  // Use full width of the column for text
  float column_width = ImGui::GetContentRegionAvail().x;

  ImGui::PushFont(imgui_drawer()->GetTitleFont());
  ImGui::TextUnformatted(entry.title_name.c_str());
  ImGui::PopFont();

  // Show file path
  std::string display_path = entry.path_to_file.string();
  float text_width = ImGui::CalcTextSize(display_path.c_str()).x;

  // Only truncate if path is too long for the column
  if (text_width > column_width) {
    // Calculate how many characters we can fit
    std::string ellipsis = "...";
    float ellipsis_width = ImGui::CalcTextSize(ellipsis.c_str()).x;
    float available_width = column_width - ellipsis_width;

    // Search for the right substring length
    size_t path_len = display_path.length();
    size_t keep_chars = path_len;
    for (size_t i = path_len; i > 0; i--) {
      std::string test_path = display_path.substr(path_len - i);
      if (ImGui::CalcTextSize(test_path.c_str()).x <= available_width) {
        keep_chars = i;
        break;
      }
    }
    display_path = ellipsis + display_path.substr(path_len - keep_chars);
  }
  ImGui::TextUnformatted(display_path.c_str());

  ImGui::SetCursorPosY(start_position.y + ui::default_image_icon_size.y -
                       ImGui::GetTextLineHeight());

  if (entry.last_run_time != 0) {
    ImGui::TextUnformatted(
        fmt::format("Last played: {:%Y-%m-%d %H:%M}",
                    std::chrono::system_clock::time_point(
                        std::chrono::seconds(entry.last_run_time)))
            .c_str());
  } else {
    ImGui::TextUnformatted("Last played: Unknown");
  }

  // Create invisible selectable over the entire row
  ImGui::SetCursorPos(start_position);

  // Use index for unique ID instead of title_id which might be 0
  if (ImGui::Selectable(fmt::format("##RecentTitle{}Selectable", index).c_str(),
                        selected_title_ == entry.title_id,
                        ImGuiSelectableFlags_SpanAllColumns |
                            ImGuiSelectableFlags_AllowOverlap,
                        ImVec2(0, ui::default_image_icon_size.y))) {
    selected_title_ = entry.title_id;
    LaunchTitle(entry.path_to_file);
  }

  if (ImGui::BeginPopupContextItem(
          fmt::format("Recent Title Menu {}", index).c_str())) {
    selected_title_ = entry.title_id;

    if (ImGui::MenuItem("Launch")) {
      LaunchTitle(entry.path_to_file);
    }

    if (ImGui::MenuItem("Open containing folder")) {
      std::filesystem::path folder = entry.path_to_file.parent_path();
      std::thread path_open(LaunchFileExplorer, folder);
      path_open.detach();
    }

    ImGui::EndPopup();
  }
}

void RecentTitlesUI::LaunchTitle(const std::filesystem::path& path) {
  if (emulator_window_) {
    if (emulator_window_->HasRunningChildProcess()) {
      return;
    }
    emulator_window_->LaunchTitleInNewProcess(path);
  }
}

void RecentTitlesUI::OnDraw(ImGuiIO& io) {
  // Check if the number of logged-in profiles has changed
  has_logged_in_profile_ = false;
  if (emulator_window_ && emulator_window_->emulator()) {
    auto kernel_state = emulator_window_->emulator()->kernel_state();
    if (kernel_state) {
      auto xam_state = kernel_state->xam_state();
      if (xam_state) {
        auto profile_manager = xam_state->profile_manager();
        if (profile_manager) {
          // Count currently logged-in profiles
          int current_logged_in_count = 0;
          for (uint8_t i = 0; i < 4; i++) {
            if (profile_manager->GetProfile(i)) {
              current_logged_in_count++;
              has_logged_in_profile_ = true;
            }
          }

          // If the count changed, refresh icons
          if (current_logged_in_count != last_logged_in_count_) {
            XELOGI(
                "Logged-in profile count changed from {} to {}, refreshing "
                "icons",
                last_logged_in_count_, current_logged_in_count);
            last_logged_in_count_ = current_logged_in_count;

            // Refresh icons when profiles change (login or logout)
            RefreshIcons();
          }
        }
      }
    }
  }

  TryLoadIcons();

  // Make the window take up the entire visible area
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size, ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(1.0f);

  if (!ImGui::Begin("##RecentlyPlayedGames", nullptr,
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoBringToFrontOnFocus)) {
    ImGui::End();
    return;
  }

  if (!recent_titles_.empty()) {
    if (recent_titles_.size() > 5) {
      ImGui::Text("Search: ");
      ImGui::SameLine();
      ImGui::InputText("##Search", title_name_filter_, title_name_filter_size);
      ImGui::Separator();
    }

    if (ImGui::BeginTable(
            "", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX)) {
      // Set the icon column to fixed width (icon + small padding)
      ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed,
                              ui::default_image_icon_size.x + 10.0f);
      // The details column takes the remaining space
      ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch);

      size_t display_index = 0;
      bool first_item = true;
      for (auto& entry : recent_titles_) {
        std::string filter(title_name_filter_);
        if (!filter.empty()) {
          bool contains_filter =
              utf8::lower_ascii(entry.title_name)
                      .find(utf8::lower_ascii(filter)) != std::string::npos ||
              utf8::lower_ascii(entry.path_to_file.string())
                      .find(utf8::lower_ascii(filter)) != std::string::npos;

          if (!contains_filter) {
            continue;
          }
        }
        // Add row with vertical padding
        ImGui::TableNextRow(0, ui::default_image_icon_size.y + 4.0f);
        DrawTitleEntry(io, entry, display_index++);
        first_item = false;
      }
      ImGui::EndTable();
    }
  } else {
    // Align text to the center
    std::string no_entries_message = "No recently played games found.";

    ImGui::PushFont(imgui_drawer()->GetTitleFont());
    float windowWidth = ImGui::GetContentRegionAvail().x;
    ImVec2 textSize = ImGui::CalcTextSize(no_entries_message.c_str());
    float textOffsetX = (windowWidth - textSize.x) * 0.5f;
    if (textOffsetX > 0.0f) {
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + textOffsetX);
    }

    ImGui::Text("%s", no_entries_message.c_str());
    ImGui::PopFont();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Open Game...")) {
      emulator_window_->FileOpen();
    }
  }

  ImGui::End();
}

}  // namespace app
}  // namespace xe
