/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/launch_data_restart_dialog_qt.h"

#include <QLabel>
#include <QVBoxLayout>
#include <cstdlib>

#include "xenia/kernel/kernel_state.h"
#include "xenia/ui/window.h"

namespace xe {
namespace app {

LaunchDataRestartDialogQt::LaunchDataRestartDialogQt(
    QWidget* parent, ui::Window* display_window,
    kernel::KernelState* kernel_state)
    : QDialog(parent),
      display_window_(display_window),
      kernel_state_(kernel_state) {
  SetupUI();
}

LaunchDataRestartDialogQt::~LaunchDataRestartDialogQt() {
  // Cleanup handled by Qt
}

void LaunchDataRestartDialogQt::SetupUI() {
  setWindowTitle("Title Restart Required");
  setModal(true);
  setAttribute(Qt::WA_DeleteOnClose);
  setMinimumWidth(400);

  auto* main_layout = new QVBoxLayout(this);

  // Message text
  auto* message_label = new QLabel(
      "Title is restarting with new launch data.\n"
      "Click OK to continue. Game will be loaded automatically.",
      this);
  message_label->setWordWrap(true);
  main_layout->addWidget(message_label);

  main_layout->addSpacing(20);

  // OK button
  auto* button_layout = new QHBoxLayout();
  button_layout->addStretch();

  ok_button_ = new QPushButton("OK", this);
  ok_button_->setMinimumWidth(120);
  connect(ok_button_, &QPushButton::clicked, this,
          &LaunchDataRestartDialogQt::OnOKClicked);
  button_layout->addWidget(ok_button_);

  button_layout->addStretch();
  main_layout->addLayout(button_layout);
}

void LaunchDataRestartDialogQt::OnOKClicked() {
  // Terminate the title and quit after user clicks OK
  if (kernel_state_) {
    kernel_state_->TerminateTitle();
  }
  accept();

  // Use quick_exit to avoid cleanup issues
  std::quick_exit(0);
}

}  // namespace app
}  // namespace xe
