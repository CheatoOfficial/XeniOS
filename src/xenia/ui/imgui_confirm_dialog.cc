/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/imgui_confirm_dialog.h"

#include "third_party/imgui/imgui.h"
#include "xenia/hid/input_system.h"

namespace xe {
namespace ui {

ImGuiConfirmDialog::ImGuiConfirmDialog(ImGuiDrawer* drawer,
                                       const std::string& title,
                                       const std::string& message,
                                       Callback callback,
                                       hid::InputSystem* input_system)
    : ImGuiDialog(drawer),
      title_(title),
      message_(message),
      callback_(std::move(callback)),
      input_system_(input_system) {
  if (input_system_) {
    input_system_->AddUIInputBlocker();

    // Initialize prev_buttons_ to current state so held buttons aren't
    // detected as "just pressed" when the dialog opens
    hid::X_INPUT_STATE state;
    for (uint32_t user_index = 0; user_index < 4; user_index++) {
      if (input_system_->GetStateForUI(user_index, 1, &state) == 0) {
        prev_buttons_ = state.gamepad.buttons;
        break;
      }
    }
  }
}

ImGuiConfirmDialog::~ImGuiConfirmDialog() {
  if (input_system_) {
    input_system_->RemoveUIInputBlocker();
  }
}

void ImGuiConfirmDialog::PollGamepad() {
  if (!input_system_) {
    return;
  }

  for (uint32_t i = 0; i < 4; ++i) {
    hid::X_INPUT_STATE state;
    if (input_system_->GetStateForUI(i, 1, &state) == 0) {
      uint16_t buttons = state.gamepad.buttons;
      uint16_t pressed = buttons & ~prev_buttons_;

      // D-pad left/right to switch buttons
      if (pressed & 0x0004) {  // D-pad left
        focused_button_ = 0;   // No
      }
      if (pressed & 0x0008) {  // D-pad right
        focused_button_ = 1;   // Yes
      }

      // A button to confirm selection
      if (pressed & 0x1000) {
        Confirm(focused_button_ == 1);
      }

      // B button to cancel (same as No)
      if (pressed & 0x2000) {
        Confirm(false);
      }

      prev_buttons_ = buttons;
      break;
    }
  }
}

void ImGuiConfirmDialog::Confirm(bool result) {
  if (!callback_invoked_) {
    callback_invoked_ = true;
    if (callback_) {
      callback_(result);
    }
    Close();
  }
}

void ImGuiConfirmDialog::OnDraw(ImGuiIO& io) {
  // Poll gamepad input
  PollGamepad();

  // Open popup on first draw
  if (!has_opened_) {
    ImGui::OpenPopup(title_.c_str());
    has_opened_ = true;
  }

  // Style the popup
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.12f, 0.12f, 0.12f, 0.95f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.33f, 0.33f, 0.33f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

  // Center the popup on screen
  ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

  bool is_open = true;
  if (ImGui::BeginPopupModal(title_.c_str(), &is_open,
                             ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoMove)) {
    // Handle keyboard input
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      Confirm(false);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
      focused_button_ = 0;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
      focused_button_ = 1;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
      Confirm(focused_button_ == 1);
    }

    // Message text
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.82f, 0.82f, 1.0f));
    ImGui::TextWrapped("%s", message_.c_str());
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Spacing();

    // Buttons - centered
    float button_width = 90.0f;
    float spacing = 12.0f;
    float total_width = button_width * 2 + spacing;
    float start_x = (ImGui::GetWindowWidth() - total_width) * 0.5f;

    ImGui::SetCursorPosX(start_x);

    // No button
    ImVec4 no_bg = focused_button_ == 0 ? ImVec4(0.27f, 0.27f, 0.27f, 1.0f)
                                        : ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
    ImVec4 no_border = focused_button_ == 0
                           ? ImVec4(0.06f, 0.49f, 0.06f, 1.0f)  // Xbox green
                           : ImVec4(0.56f, 0.56f, 0.56f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Button, no_bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.27f, 0.27f, 0.27f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(0.27f, 0.27f, 0.27f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, no_border);

    if (ImGui::Button("No", ImVec2(button_width, 32))) {
      Confirm(false);
    }
    if (ImGui::IsItemHovered()) {
      focused_button_ = 0;
    }

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();

    ImGui::SameLine(0, spacing);

    // Yes button
    ImVec4 yes_bg = focused_button_ == 1 ? ImVec4(0.27f, 0.27f, 0.27f, 1.0f)
                                         : ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
    ImVec4 yes_border = focused_button_ == 1
                            ? ImVec4(0.06f, 0.49f, 0.06f, 1.0f)  // Xbox green
                            : ImVec4(0.56f, 0.56f, 0.56f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Button, yes_bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.27f, 0.27f, 0.27f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(0.27f, 0.27f, 0.27f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, yes_border);

    if (ImGui::Button("Yes", ImVec2(button_width, 32))) {
      Confirm(true);
    }
    if (ImGui::IsItemHovered()) {
      focused_button_ = 1;
    }

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();

    ImGui::EndPopup();
  } else {
    // Popup was closed (clicked outside or X button)
    Confirm(false);
  }

  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
}

}  // namespace ui
}  // namespace xe
