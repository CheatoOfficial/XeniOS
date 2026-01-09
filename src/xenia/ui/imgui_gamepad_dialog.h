/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_IMGUI_GAMEPAD_DIALOG_H_
#define XENIA_UI_IMGUI_GAMEPAD_DIALOG_H_

#include "xenia/ui/imgui_dialog.h"

namespace xe {
namespace hid {
class InputSystem;
}  // namespace hid
}  // namespace xe

namespace xe {
namespace ui {

// Base class for ImGui dialogs with gamepad support.
// Provides common gamepad polling and virtual methods for button handling.
class ImGuiGamepadDialog : public ImGuiDialog {
 public:
  // Set block_input to false if the dialog needs ImGui's built-in navigation
  ImGuiGamepadDialog(ImGuiDrawer* drawer, hid::InputSystem* input_system,
                     bool block_input = true);
  ~ImGuiGamepadDialog() override;

 protected:
  // Call this at the start of OnDraw() to poll gamepad input
  void PollGamepad();

  // Override to customize button behavior
  virtual void OnGamepadButtonA() {}
  virtual void OnGamepadButtonB() { Close(); }
  virtual void OnGamepadButtonX() {}
  virtual void OnGamepadButtonY() {}
  virtual void OnGamepadStart() {}
  virtual void OnGamepadBack() { Close(); }
  virtual void OnGamepadDPadUp() {}
  virtual void OnGamepadDPadDown() {}
  virtual void OnGamepadDPadLeft() {}
  virtual void OnGamepadDPadRight() {}

  hid::InputSystem* input_system_;
  uint16_t prev_buttons_ = 0;
  bool blocking_input_ = false;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_IMGUI_GAMEPAD_DIALOG_H_
