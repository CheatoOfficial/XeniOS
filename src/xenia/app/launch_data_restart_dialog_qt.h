/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_LAUNCH_DATA_RESTART_DIALOG_QT_H_
#define XENIA_APP_LAUNCH_DATA_RESTART_DIALOG_QT_H_

#include <QDialog>
#include <QPushButton>

namespace xe {
namespace ui {
class Window;
}  // namespace ui

namespace kernel {
class KernelState;
}  // namespace kernel

namespace app {

class LaunchDataRestartDialogQt : public QDialog {
  Q_OBJECT

 public:
  LaunchDataRestartDialogQt(QWidget* parent, ui::Window* display_window,
                            kernel::KernelState* kernel_state);
  ~LaunchDataRestartDialogQt() override;

 private slots:
  void OnOKClicked();

 private:
  void SetupUI();

  ui::Window* display_window_;
  kernel::KernelState* kernel_state_;
  QPushButton* ok_button_;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_LAUNCH_DATA_RESTART_DIALOG_QT_H_
