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
  }
}

ImGuiConfirmDialog::~ImGuiConfirmDialog() {
  if (input_system_) {
    input_system_->RemoveUIInputBlocker();
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
    // Handle keyboard escape or gamepad B/Back to cancel
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ShouldCloseFromGamepad()) {
      Confirm(false);
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

    // Style for buttons
    const ImVec4 button_bg(0.20f, 0.20f, 0.20f, 1.0f);
    const ImVec4 button_hover(0.27f, 0.27f, 0.27f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Button, button_bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_hover);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);

    // No button
    if (ImGui::Button("No", ImVec2(button_width, 32))) {
      Confirm(false);
    }

    ImGui::SameLine(0, spacing);

    // Yes button - set as default focus
    if (ImGui::Button("Yes", ImVec2(button_width, 32))) {
      Confirm(true);
    }
    ImGui::SetItemDefaultFocus();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

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
