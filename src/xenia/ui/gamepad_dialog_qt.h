/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_GAMEPAD_DIALOG_QT_H_
#define XENIA_UI_GAMEPAD_DIALOG_QT_H_

#include <QDialog>
#include <QMap>
#include <QTimer>
#include <QWidget>
#include <vector>

namespace xe {
namespace hid {
class InputSystem;
}  // namespace hid
}  // namespace xe

namespace xe {
namespace ui {

class GamepadDialog : public QDialog {
  Q_OBJECT

 public:
  explicit GamepadDialog(QWidget* parent, hid::InputSystem* input_system);
  ~GamepadDialog() override;

 protected:
  // Called when gamepad is connected/disconnected
  virtual void OnGamepadConnected() {}
  virtual void OnGamepadDisconnected() {}

  // Override to customize button behavior
  virtual void OnGamepadButtonA() { AcceptFocusedButton(); }
  virtual void OnGamepadButtonB() { reject(); }
  virtual void OnGamepadButtonX() {}
  virtual void OnGamepadButtonY() {}
  virtual void OnGamepadStart() { AcceptFocusedButton(); }
  virtual void OnGamepadBack() { reject(); }

  // Override to customize which widgets are focusable
  virtual bool IsWidgetGamepadFocusable(QWidget* widget) const;

  void UpdateFocusableWidgets();

  bool eventFilter(QObject* obj, QEvent* event) override;
  void showEvent(QShowEvent* event) override;

 private slots:
  void PollGamepad();

 private:
  void NavigateFocusVertical(int direction);
  void NavigateFocusHorizontal(int direction);
  void AcceptFocusedButton();
  void ApplyFocusStyle(QWidget* widget, bool focused);

  hid::InputSystem* input_system_;
  QTimer* poll_timer_;
  std::vector<QWidget*> focusable_widgets_;
  int current_focus_index_;

  // Previous button states for edge detection and repeat
  uint16_t prev_buttons_;
  int repeat_counter_;        // Counts polls while button held for repeat
                              // functionality
  float scroll_accumulator_;  // Accumulates fractional scrolling

  QMap<QWidget*, QString> original_stylesheets_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GAMEPAD_DIALOG_QT_H_
