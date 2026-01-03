/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/confirm_dialog_widget_qt.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QVBoxLayout>

#include "xenia/hid/input_system.h"

namespace xe {
namespace app {

ConfirmDialogWidgetQt::ConfirmDialogWidgetQt(QWidget* parent,
                                             hid::InputSystem* input_system,
                                             const QString& title,
                                             const QString& message)
    : QDialog(parent),
      input_system_(input_system),
      poll_timer_(nullptr),
      focused_index_(0),  // Start with "No" focused (safer default)
      prev_buttons_(0) {
  setWindowTitle(title);
  setModal(true);
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

  // Match theme from postprocessing dialog
  setStyleSheet(
      "QDialog { background-color: rgb(30, 30, 30); }"
      "QLabel { color: #d0d0d0; background: transparent; }");

  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(24, 20, 24, 20);
  main_layout->setSpacing(16);

  // Message
  auto* message_label = new QLabel(message, this);
  message_label->setStyleSheet("color: #d0d0d0; font-size: 12px;");
  message_label->setAlignment(Qt::AlignCenter);
  message_label->setWordWrap(true);
  main_layout->addWidget(message_label);

  main_layout->addSpacing(8);

  // Buttons
  auto* button_layout = new QHBoxLayout();
  button_layout->setSpacing(12);

  no_button_ = new QPushButton("No", this);
  no_button_->setMinimumSize(90, 32);
  no_button_->setCursor(Qt::PointingHandCursor);
  connect(no_button_, &QPushButton::clicked, this, &QDialog::reject);

  yes_button_ = new QPushButton("Yes", this);
  yes_button_->setMinimumSize(90, 32);
  yes_button_->setCursor(Qt::PointingHandCursor);
  connect(yes_button_, &QPushButton::clicked, this, &QDialog::accept);

  button_layout->addStretch();
  button_layout->addWidget(no_button_);
  button_layout->addWidget(yes_button_);
  button_layout->addStretch();

  main_layout->addLayout(button_layout);

  // Start gamepad polling if input system is available
  if (input_system_) {
    input_system_->AddUIInputBlocker();

    // Initialize prev_buttons_ to current state
    hid::X_INPUT_STATE state;
    for (uint32_t user_index = 0; user_index < 4; user_index++) {
      if (input_system_->GetStateForUI(user_index, 1, &state) == 0) {
        prev_buttons_ = state.gamepad.buttons;
        break;
      }
    }

    poll_timer_ = new QTimer(this);
    connect(poll_timer_, &QTimer::timeout, this,
            &ConfirmDialogWidgetQt::PollGamepad);
    poll_timer_->start(16);  // ~60fps polling
  }

  // Set initial button focus
  UpdateFocusedButton(focused_index_);
}

ConfirmDialogWidgetQt::~ConfirmDialogWidgetQt() {
  if (poll_timer_) {
    poll_timer_->stop();
  }
  if (input_system_) {
    input_system_->RemoveUIInputBlocker();
  }
}

bool ConfirmDialogWidgetQt::Confirm(QWidget* parent,
                                    hid::InputSystem* input_system,
                                    const QString& title,
                                    const QString& message) {
  ConfirmDialogWidgetQt dialog(parent, input_system, title, message);
  return dialog.exec() == QDialog::Accepted;
}

void ConfirmDialogWidgetQt::keyPressEvent(QKeyEvent* event) {
  switch (event->key()) {
    case Qt::Key_Escape:
      reject();
      break;
    case Qt::Key_Left:
    case Qt::Key_Right:
      UpdateFocusedButton(focused_index_ == 0 ? 1 : 0);
      break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      if (focused_index_ == 1) {
        accept();
      } else {
        reject();
      }
      break;
    default:
      QDialog::keyPressEvent(event);
  }
}

void ConfirmDialogWidgetQt::PollGamepad() {
  if (!input_system_) {
    return;
  }

  for (uint32_t i = 0; i < 4; ++i) {
    hid::X_INPUT_STATE state;
    if (input_system_->GetStateForUI(i, 1, &state) == 0) {
      uint16_t buttons = state.gamepad.buttons;
      uint16_t pressed = buttons & ~prev_buttons_;

      // D-pad left/right to switch buttons
      if (pressed & 0x0004) {    // D-pad left
        UpdateFocusedButton(0);  // No
      }
      if (pressed & 0x0008) {    // D-pad right
        UpdateFocusedButton(1);  // Yes
      }

      // A button to confirm selection
      if (pressed & 0x1000) {
        if (focused_index_ == 1) {
          accept();
        } else {
          reject();
        }
      }

      // B button to cancel (same as No)
      if (pressed & 0x2000) {
        reject();
      }

      prev_buttons_ = buttons;
      break;
    }
  }
}

void ConfirmDialogWidgetQt::UpdateFocusedButton(int index) {
  focused_index_ = index;

  // Theme-consistent button styles
  QString unfocused_style =
      "QPushButton { background-color: rgba(50, 50, 50, 200); color: #d0d0d0; "
      "border: 2px solid #909090; border-radius: 4px; "
      "font-size: 12px; padding: 6px 16px; }"
      "QPushButton:hover { background-color: rgba(70, 70, 70, 200); "
      "border-color: #b0b0b0; }";

  QString focused_style =
      "QPushButton { background-color: rgba(70, 70, 70, 200); color: #f0f0f0; "
      "border: 2px solid #107c10; border-radius: 4px; "
      "font-size: 12px; padding: 6px 16px; }";

  if (focused_index_ == 0) {
    no_button_->setStyleSheet(focused_style);
    yes_button_->setStyleSheet(unfocused_style);
  } else {
    no_button_->setStyleSheet(unfocused_style);
    yes_button_->setStyleSheet(focused_style);
  }
}

}  // namespace app
}  // namespace xe
