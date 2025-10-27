/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_NOTIFICATION_WIDGET_QT_H_
#define XENIA_APP_NOTIFICATION_WIDGET_QT_H_

#include <QLabel>
#include <QTimer>
#include <QWidget>

namespace xe {
namespace app {

class NotificationWidgetQt : public QWidget {
  Q_OBJECT

 public:
  NotificationWidgetQt(QWidget* parent, const QString& title,
                       const QString& message, int duration_ms = 3000);
  ~NotificationWidgetQt() override = default;

  void Show();

 private:
  QTimer* auto_close_timer_;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_NOTIFICATION_WIDGET_QT_H_
