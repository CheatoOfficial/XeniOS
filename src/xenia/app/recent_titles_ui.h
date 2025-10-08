/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_RECENT_TITLES_UI_H_
#define XENIA_APP_RECENT_TITLES_UI_H_

#include <filesystem>
#include <map>
#include <memory>
#include <vector>
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"

namespace xe {
namespace app {

class EmulatorWindow;

struct RecentTitleDisplay {
  std::string title_name;
  std::filesystem::path path_to_file;
  time_t last_run_time;
  uint32_t title_id;
  std::vector<uint8_t> icon;
};

class RecentTitlesUI final : public ui::ImGuiDialog {
 public:
  RecentTitlesUI(ui::ImGuiDrawer* imgui_drawer,
                 EmulatorWindow* emulator_window);

  ~RecentTitlesUI();

 public:
  void LoadRecentTitles();

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void TryLoadIcons();
  void DrawTitleEntry(ImGuiIO& io, RecentTitleDisplay& entry, size_t index);
  void LaunchTitle(const std::filesystem::path& path);

 public:
  void RefreshIcons();

  static constexpr uint8_t title_name_filter_size = 15;

  char title_name_filter_[title_name_filter_size] = "";
  uint32_t selected_title_ = 0;
  int last_logged_in_count_ = 0;
  bool has_logged_in_profile_ = false;

  EmulatorWindow* emulator_window_;
  std::vector<RecentTitleDisplay> recent_titles_;
  std::map<uint32_t, std::unique_ptr<ui::ImmediateTexture>> title_icons_;
};

}  // namespace app
}  // namespace xe

#endif
