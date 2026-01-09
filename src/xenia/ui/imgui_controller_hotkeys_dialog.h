/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_IMGUI_CONTROLLER_HOTKEYS_DIALOG_H_
#define XENIA_UI_IMGUI_CONTROLLER_HOTKEYS_DIALOG_H_

#include <functional>

#include "xenia/ui/imgui_gamepad_dialog.h"

namespace xe {
namespace app {
class EmulatorWindow;
}  // namespace app
}  // namespace xe

namespace xe {
namespace ui {

// ImGui-based controller hotkeys dialog.
class ImGuiControllerHotkeysDialog : public ImGuiGamepadDialog {
 public:
  ImGuiControllerHotkeysDialog(ImGuiDrawer* drawer,
                               app::EmulatorWindow* emulator_window,
                               hid::InputSystem* input_system);

  void CloseDialog() { Close(); }

  void SetOnCloseCallback(std::function<void()> callback) {
    on_close_callback_ = std::move(callback);
  }

 protected:
  void OnClose() override;
  void OnDraw(ImGuiIO& io) override;

 private:
  app::EmulatorWindow* emulator_window_;
  std::function<void()> on_close_callback_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_IMGUI_CONTROLLER_HOTKEYS_DIALOG_H_
