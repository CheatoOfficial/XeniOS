/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/controller_hotkeys_dialog_qt.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QStyle>

#include "xenia/app/emulator_window.h"

namespace xe {
namespace app {

ControllerHotkeysDialogQt::ControllerHotkeysDialogQt(
    QWidget* parent, EmulatorWindow* emulator_window,
    hid::InputSystem* input_system)
    : ui::GamepadDialog(parent, input_system),
      emulator_window_(emulator_window) {
  SetupUI();

  // Position near top, centered horizontally
  if (parent) {
    QPoint parent_pos = parent->mapToGlobal(QPoint(0, 0));
    int center_x = parent_pos.x() + (parent->width() - width()) / 2;
    move(center_x, parent_pos.y() + 20);
  }
}

ControllerHotkeysDialogQt::~ControllerHotkeysDialogQt() = default;

void ControllerHotkeysDialogQt::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    close();
    return;
  }
  QDialog::keyPressEvent(event);
}

void ControllerHotkeysDialogQt::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    QWidget* child = childAt(event->position().toPoint());
    if (!child || child == this) {
      drag_position_ =
          event->globalPosition().toPoint() - frameGeometry().topLeft();
      dragging_ = true;
      event->accept();
      return;
    }
  }
  QDialog::mousePressEvent(event);
}

void ControllerHotkeysDialogQt::mouseMoveEvent(QMouseEvent* event) {
  if (dragging_ && (event->buttons() & Qt::LeftButton)) {
    move(event->globalPosition().toPoint() - drag_position_);
    event->accept();
    return;
  }
  QDialog::mouseMoveEvent(event);
}

void ControllerHotkeysDialogQt::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    dragging_ = false;
  }
  QDialog::mouseReleaseEvent(event);
}

void ControllerHotkeysDialogQt::SetupUI() {
  setWindowTitle("Controller Hotkeys");
  setModal(false);
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
  setMinimumWidth(400);

  setWindowOpacity(0.92);

  setStyleSheet(R"(
    QDialog {
      background-color: rgb(30, 30, 30);
      border: 1px solid rgba(100, 100, 100, 180);
      padding: 0px;
    }
    QLabel {
      color: #d0d0d0;
      background-color: transparent;
    }
  )");

  auto* content_layout = new QVBoxLayout(this);
  content_layout->setContentsMargins(16, 16, 16, 16);
  content_layout->setSpacing(8);

  // Top bar with title and close button
  auto* top_bar_layout = new QHBoxLayout();
  auto* title_label = new QLabel("Controller Hotkeys", this);
  title_label->setStyleSheet(
      "color: #f0f0f0; font-weight: bold; font-size: 14px;");
  top_bar_layout->addWidget(title_label);
  top_bar_layout->addStretch();

  auto* close_button = new QPushButton(this);
  close_button->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
  close_button->setIconSize(QSize(16, 16));
  close_button->setFlat(true);
  close_button->setStyleSheet(R"(
    QPushButton {
      background-color: transparent;
      border: none;
      min-width: 24px;
      max-width: 24px;
      min-height: 24px;
      max-height: 24px;
      padding: 2px;
    }
    QPushButton:hover {
      background-color: rgba(200, 50, 50, 180);
      border-radius: 4px;
    }
    QPushButton:pressed {
      background-color: rgba(150, 30, 30, 220);
      border-radius: 4px;
    }
  )");
  close_button->setToolTip("Close");
  connect(close_button, &QPushButton::clicked, this, &QDialog::close);
  top_bar_layout->addWidget(close_button, 0, Qt::AlignTop);

  content_layout->addLayout(top_bar_layout);
  content_layout->addSpacing(8);

  // Get hotkeys from emulator window
  auto hotkeys = emulator_window_->GetControllerHotkeysList();

  for (const auto& [text, enabled] : hotkeys) {
    auto* label = new QLabel(QString::fromStdString(text), this);
    if (!enabled) {
      label->setStyleSheet("color: #666666;");
    }
    content_layout->addWidget(label);
  }

  content_layout->addStretch();
}

}  // namespace app
}  // namespace xe
