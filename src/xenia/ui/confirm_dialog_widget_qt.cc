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
                                             const QString& message,
                                             std::function<void(bool)> callback)
    : QWidget(parent),
      callback_(callback),
      input_system_(input_system),
      poll_timer_(nullptr),
      focused_index_(0),  // Start with "No" focused (safer default)
      prev_buttons_(0) {
  setAttribute(Qt::WA_DeleteOnClose);
  setAutoFillBackground(true);
  setAttribute(Qt::WA_StyledBackground, true);

  // Match theme from postprocessing dialog
  setStyleSheet(
      "background-color: rgb(30, 30, 30); "
      "border: 1px solid rgba(100, 100, 100, 180);");

  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(24, 20, 24, 20);
  main_layout->setSpacing(16);

  // Title
  auto* title_label = new QLabel(title, this);
  title_label->setStyleSheet(
      "color: #f0f0f0; font-size: 15px; font-weight: bold; "
      "background: transparent; border: none;");
  title_label->setAlignment(Qt::AlignCenter);
  main_layout->addWidget(title_label);

  // Message
  auto* message_label = new QLabel(message, this);
  message_label->setStyleSheet(
      "color: #d0d0d0; font-size: 12px; "
      "background: transparent; border: none;");
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
  connect(no_button_, &QPushButton::clicked, this, [this]() {
    close();
    if (callback_) callback_(false);
  });

  yes_button_ = new QPushButton("Yes", this);
  yes_button_->setMinimumSize(90, 32);
  yes_button_->setCursor(Qt::PointingHandCursor);
  connect(yes_button_, &QPushButton::clicked, this, [this]() {
    close();
    if (callback_) callback_(true);
  });

  button_layout->addStretch();
  button_layout->addWidget(no_button_);
  button_layout->addWidget(yes_button_);
  button_layout->addStretch();

  main_layout->addLayout(button_layout);

  // Install global event filter to close on clicks outside
  qApp->installEventFilter(this);

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
}

ConfirmDialogWidgetQt::~ConfirmDialogWidgetQt() {
  if (poll_timer_) {
    poll_timer_->stop();
  }
  if (input_system_) {
    input_system_->RemoveUIInputBlocker();
  }
}

void ConfirmDialogWidgetQt::ShowCentered() {
  if (!parentWidget()) {
    return;
  }

  adjustSize();

  // Center in parent
  QWidget* parent = parentWidget();
  int x = (parent->width() - width()) / 2;
  int y = (parent->height() - height()) / 2;

  move(x, y);

  show();
  raise();
  setFocus(Qt::PopupFocusReason);

  // Set initial button focus
  UpdateFocusedButton(focused_index_);
}

void ConfirmDialogWidgetQt::keyPressEvent(QKeyEvent* event) {
  switch (event->key()) {
    case Qt::Key_Escape:
      close();
      if (callback_) callback_(false);
      break;
    case Qt::Key_Left:
    case Qt::Key_Right:
      UpdateFocusedButton(focused_index_ == 0 ? 1 : 0);
      break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      ActivateFocusedButton();
      break;
    default:
      QWidget::keyPressEvent(event);
  }
}

bool ConfirmDialogWidgetQt::eventFilter(QObject* obj, QEvent* event) {
  if (event->type() == QEvent::MouseButtonPress) {
    QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

    if (mouseEvent->button() == Qt::LeftButton) {
      QWidget* widget = qobject_cast<QWidget*>(obj);
      if (widget) {
        QPoint globalPos = widget->mapToGlobal(mouseEvent->pos());
        QPoint localPos = mapFromGlobal(globalPos);

        if (!rect().contains(localPos)) {
          close();
          if (callback_) callback_(false);
          return true;
        }
      }
    }
  }
  return false;
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
        ActivateFocusedButton();
      }

      // B button to cancel (same as No)
      if (pressed & 0x2000) {
        close();
        if (callback_) callback_(false);
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

void ConfirmDialogWidgetQt::ActivateFocusedButton() {
  bool result = (focused_index_ == 1);  // Yes = 1
  close();
  if (callback_) {
    QTimer::singleShot(0, [this, result]() {
      if (callback_) callback_(result);
    });
  }
}

}  // namespace app
}  // namespace xe
