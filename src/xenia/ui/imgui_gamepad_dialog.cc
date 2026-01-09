/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/imgui_gamepad_dialog.h"

#include "xenia/hid/input.h"
#include "xenia/hid/input_system.h"

namespace xe {
namespace ui {

ImGuiGamepadDialog::ImGuiGamepadDialog(ImGuiDrawer* drawer,
                                       hid::InputSystem* input_system,
                                       bool block_input)
    : ImGuiDialog(drawer), input_system_(input_system) {
  if (input_system_) {
    if (block_input) {
      input_system_->AddUIInputBlocker();
      blocking_input_ = true;
    }

    // Initialize prev_buttons_ to current state to avoid spurious triggers
    hid::X_INPUT_STATE state;
    for (uint32_t user_index = 0; user_index < 4; user_index++) {
      if (input_system_->GetStateForUI(user_index, 1, &state) == 0) {
        prev_buttons_ = state.gamepad.buttons;
        break;
      }
    }
  }
}

ImGuiGamepadDialog::~ImGuiGamepadDialog() {
  if (input_system_ && blocking_input_) {
    input_system_->RemoveUIInputBlocker();
  }
}

void ImGuiGamepadDialog::PollGamepad() {
  if (!input_system_) {
    return;
  }

  for (uint32_t i = 0; i < 4; ++i) {
    hid::X_INPUT_STATE state;
    if (input_system_->GetStateForUI(i, 1, &state) == 0) {
      uint16_t buttons = state.gamepad.buttons;
      uint16_t pressed = buttons & ~prev_buttons_;

      // D-pad
      if (pressed & 0x0001) OnGamepadDPadUp();
      if (pressed & 0x0002) OnGamepadDPadDown();
      if (pressed & 0x0004) OnGamepadDPadLeft();
      if (pressed & 0x0008) OnGamepadDPadRight();

      // Face buttons
      if (pressed & 0x1000) OnGamepadButtonA();
      if (pressed & 0x2000) OnGamepadButtonB();
      if (pressed & 0x4000) OnGamepadButtonX();
      if (pressed & 0x8000) OnGamepadButtonY();

      // Start/Back
      if (pressed & 0x0010) OnGamepadStart();
      if (pressed & 0x0020) OnGamepadBack();

      prev_buttons_ = buttons;
      break;
    }
  }
}

}  // namespace ui
}  // namespace xe
