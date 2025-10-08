/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/notification_widget_qt.h"

#include <QGraphicsOpacityEffect>
#include <QVBoxLayout>

namespace xe {
namespace app {

NotificationWidgetQt::NotificationWidgetQt(QWidget* parent,
                                           const QString& title,
                                           const QString& message,
                                           int duration_ms)
    : QWidget(parent) {
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
  setAttribute(Qt::WA_TranslucentBackground);

  // Create layout
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(15, 15, 15, 15);

  // Container widget with background
  auto* container = new QWidget(this);
  container->setStyleSheet(
      "background-color: rgba(40, 40, 40, 230); "
      "border-radius: 8px; "
      "padding: 10px;");

  auto* container_layout = new QVBoxLayout(container);

  // Title label
  auto* title_label = new QLabel(title);
  QFont title_font = title_label->font();
  title_font.setBold(true);
  title_font.setPointSize(title_font.pointSize() + 2);
  title_label->setFont(title_font);
  title_label->setStyleSheet("color: white;");
  container_layout->addWidget(title_label);

  // Message label
  auto* message_label = new QLabel(message);
  message_label->setStyleSheet("color: #d0d0d0;");
  message_label->setWordWrap(true);
  container_layout->addWidget(message_label);

  layout->addWidget(container);

  // Set fixed width
  setFixedWidth(300);
  adjustSize();

  // Setup fade animation
  auto* opacity_effect = new QGraphicsOpacityEffect(this);
  setGraphicsEffect(opacity_effect);

  fade_animation_ = new QPropertyAnimation(opacity_effect, "opacity", this);
  fade_animation_->setDuration(500);
  fade_animation_->setStartValue(1.0);
  fade_animation_->setEndValue(0.0);
  connect(fade_animation_, &QPropertyAnimation::finished, this,
          &NotificationWidgetQt::OnFadeOutFinished);

  // Setup auto-close timer
  auto_close_timer_ = new QTimer(this);
  auto_close_timer_->setSingleShot(true);
  connect(auto_close_timer_, &QTimer::timeout,
          [this]() { fade_animation_->start(); });
  auto_close_timer_->setInterval(duration_ms);
}

void NotificationWidgetQt::Show() {
  if (!parentWidget()) {
    return;
  }

  // Position in bottom-right corner with some padding
  // Use global screen coordinates
  QWidget* parent = parentWidget();
  QRect parent_geo = parent->geometry();

  int x = parent_geo.right() - width() - 20;
  int y = parent_geo.bottom() - height() - 20;

  // Convert to global coordinates
  QPoint global_pos = parent->mapToGlobal(
      QPoint(parent->width() - width() - 20, parent->height() - height() - 20));

  move(global_pos);

  show();
  raise();
  auto_close_timer_->start();
}

void NotificationWidgetQt::OnFadeOutFinished() { close(); }

}  // namespace app
}  // namespace xe
