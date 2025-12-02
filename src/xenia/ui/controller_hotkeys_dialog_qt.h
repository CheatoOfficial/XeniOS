/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_CONTROLLER_HOTKEYS_DIALOG_QT_H_
#define XENIA_UI_CONTROLLER_HOTKEYS_DIALOG_QT_H_

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "xenia/ui/gamepad_dialog_qt.h"

namespace xe {
namespace app {

class EmulatorWindow;

class ControllerHotkeysDialogQt : public ui::GamepadDialog {
  Q_OBJECT

 public:
  ControllerHotkeysDialogQt(QWidget* parent, EmulatorWindow* emulator_window,
                            hid::InputSystem* input_system);
  ~ControllerHotkeysDialogQt() override;

 protected:
  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  void SetupUI();

  EmulatorWindow* emulator_window_;
  QPoint drag_position_;
  bool dragging_ = false;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_UI_CONTROLLER_HOTKEYS_DIALOG_QT_H_
