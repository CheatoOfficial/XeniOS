/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/notification_widget_qt.h"

#include <QVBoxLayout>

namespace xe {
namespace app {

NotificationWidgetQt::NotificationWidgetQt(QWidget* parent,
                                           const QString& title,
                                           const QString& message,
                                           int duration_ms)
    : QWidget(parent) {
  setAttribute(Qt::WA_DeleteOnClose);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAutoFillBackground(true);
  setAttribute(Qt::WA_StyledBackground, true);

  // Simple solid background
  setStyleSheet("background-color: rgb(40, 40, 40);");

  // Layout with padding
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(15, 15, 15, 15);

  // Title label
  auto* title_label = new QLabel(title);
  QFont title_font = title_label->font();
  title_font.setBold(true);
  title_font.setPointSize(title_font.pointSize() + 2);
  title_label->setFont(title_font);
  title_label->setStyleSheet("color: white; background: transparent;");
  layout->addWidget(title_label);

  // Message label
  auto* message_label = new QLabel(message);
  message_label->setStyleSheet("color: #d0d0d0; background: transparent;");
  message_label->setWordWrap(true);
  layout->addWidget(message_label);

  // Set fixed width
  setFixedWidth(300);
  adjustSize();

  // Setup auto-close timer
  auto_close_timer_ = new QTimer(this);
  auto_close_timer_->setSingleShot(true);
  connect(auto_close_timer_, &QTimer::timeout, [this]() {
    deleteLater();  // Ensure widget is actually destroyed
  });
  auto_close_timer_->setInterval(duration_ms);
}

void NotificationWidgetQt::Show() {
  if (!parentWidget()) {
    return;
  }

  // Position in bottom-right corner with some padding (relative to parent)
  QWidget* parent = parentWidget();
  int x = parent->width() - width() - 20;
  int y = parent->height() - height() - 20;

  move(x, y);

  show();
  raise();
  auto_close_timer_->start();
}

}  // namespace app
}  // namespace xe
