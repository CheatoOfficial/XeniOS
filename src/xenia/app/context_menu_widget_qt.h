/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_CONTEXT_MENU_WIDGET_QT_H_
#define XENIA_APP_CONTEXT_MENU_WIDGET_QT_H_

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include <vector>

namespace xe {
namespace app {

// Context menu widget that works like the toast - as a child widget overlay
// instead of a separate window to avoid Windows compositor issues
class ContextMenuWidgetQt : public QWidget {
  Q_OBJECT

 public:
  ContextMenuWidgetQt(QWidget* parent);
  ~ContextMenuWidgetQt() override = default;

  void AddAction(const QString& text, std::function<void()> callback);
  void AddSeparator();
  void ShowAt(const QPoint& global_pos);

 protected:
  void keyPressEvent(QKeyEvent* event) override;
  bool eventFilter(QObject* obj, QEvent* event) override;

 private:
  QVBoxLayout* layout_;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_CONTEXT_MENU_WIDGET_QT_H_
