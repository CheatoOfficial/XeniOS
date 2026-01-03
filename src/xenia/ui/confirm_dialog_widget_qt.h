/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_CONFIRM_DIALOG_WIDGET_QT_H_
#define XENIA_UI_CONFIRM_DIALOG_WIDGET_QT_H_

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <functional>

namespace xe {
namespace hid {
class InputSystem;
}  // namespace hid
}  // namespace xe

namespace xe {
namespace app {

// Confirmation dialog with gamepad support
class ConfirmDialogWidgetQt : public QDialog {
  Q_OBJECT

 public:
  ConfirmDialogWidgetQt(QWidget* parent, hid::InputSystem* input_system,
                        const QString& title, const QString& message);
  ~ConfirmDialogWidgetQt() override;

  // Static blocking method - returns true if confirmed
  static bool Confirm(QWidget* parent, hid::InputSystem* input_system,
                      const QString& title, const QString& message);

 protected:
  void keyPressEvent(QKeyEvent* event) override;

 private slots:
  void PollGamepad();

 private:
  void UpdateFocusedButton(int index);

  QPushButton* yes_button_;
  QPushButton* no_button_;

  // Gamepad support
  hid::InputSystem* input_system_;
  QTimer* poll_timer_;
  int focused_index_;  // 0 = No, 1 = Yes
  uint16_t prev_buttons_;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_UI_CONFIRM_DIALOG_WIDGET_QT_H_
