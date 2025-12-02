/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_XMP_DIALOG_QT_H_
#define XENIA_UI_XMP_DIALOG_QT_H_

#include <QLabel>
#include <QPushButton>
#include <QSlider>

#include "xenia/ui/gamepad_dialog_qt.h"

namespace xe {
namespace app {

class EmulatorWindow;

class XmpDialogQt : public ui::GamepadDialog {
  Q_OBJECT

 public:
  XmpDialogQt(QWidget* parent, EmulatorWindow* emulator_window,
              hid::InputSystem* input_system);
  ~XmpDialogQt() override;

 private slots:
  void OnPlayPauseClicked();
  void OnVolumeChanged(int value);

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void showEvent(QShowEvent* event) override;

 private:
  void SetupUI();
  void UpdatePlayerState();

  EmulatorWindow* emulator_window_;
  QPoint drag_position_;

  // Status widgets
  QLabel* status_label_;
  QLabel* status_value_label_;

  // Playback control widgets
  QPushButton* play_pause_button_;

  // Volume widgets
  QLabel* volume_label_;
  QSlider* volume_slider_;
  QLabel* volume_value_label_;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_UI_XMP_DIALOG_QT_H_
