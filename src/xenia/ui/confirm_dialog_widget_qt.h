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

#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QWidget>
#include <functional>

namespace xe {
namespace hid {
class InputSystem;
}  // namespace hid
}  // namespace xe

namespace xe {
namespace app {

// Confirmation dialog widget with gamepad support
class ConfirmDialogWidgetQt : public QWidget {
  Q_OBJECT

 public:
  ConfirmDialogWidgetQt(QWidget* parent, hid::InputSystem* input_system,
                        const QString& title, const QString& message,
                        std::function<void(bool)> callback);
  ~ConfirmDialogWidgetQt() override;

  void ShowCentered();

 protected:
  void keyPressEvent(QKeyEvent* event) override;
  bool eventFilter(QObject* obj, QEvent* event) override;

 private slots:
  void PollGamepad();

 private:
  void UpdateFocusedButton(int index);
  void ActivateFocusedButton();

  std::function<void(bool)> callback_;
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
