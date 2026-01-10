/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/imgui_controller_hotkeys_dialog.h"

#include "third_party/imgui/imgui.h"
#include "xenia/app/emulator_window.h"

namespace xe {
namespace ui {

ImGuiControllerHotkeysDialog::ImGuiControllerHotkeysDialog(
    ImGuiDrawer* drawer, app::EmulatorWindow* emulator_window,
    hid::InputSystem* input_system)
    : ImGuiGamepadDialog(drawer, input_system),
      emulator_window_(emulator_window) {}

void ImGuiControllerHotkeysDialog::OnClose() {
  if (on_close_callback_) {
    on_close_callback_();
  }
}

void ImGuiControllerHotkeysDialog::OnDraw(ImGuiIO& io) {
  // Style - white background, black text, Xbox green accents
  const ImVec4 xbox_green(0.063f, 0.486f, 0.063f, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_TitleBg, xbox_green);
  ImGui::PushStyleColor(ImGuiCol_TitleBgActive, xbox_green);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

  // Center on screen
  ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Always);

  bool is_open = true;
  if (ImGui::Begin("Controller Hotkeys", &is_open,
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoCollapse)) {
    // Handle keyboard escape or gamepad B/Back
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ShouldCloseFromGamepad()) {
      Close();
    }

    // Colors for content
    ImVec4 disabled_text = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);

    // Title
    ImGui::PushStyleColor(ImGuiCol_Text, xbox_green);
    ImGui::Text("Available Hotkeys");
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Get hotkeys from emulator window
    auto hotkeys = emulator_window_->GetControllerHotkeysList();

    for (const auto& [text, enabled] : hotkeys) {
      if (!enabled) {
        ImGui::PushStyleColor(ImGuiCol_Text, disabled_text);
      }
      ImGui::Text("%s", text.c_str());
      if (!enabled) {
        ImGui::PopStyleColor();
      }
    }

    ImGui::End();
  }

  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor(6);

  if (!is_open) {
    Close();
  }
}

}  // namespace ui
}  // namespace xe
