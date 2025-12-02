/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_CONTEXT_MENU_WIDGET_QT_H_
#define XENIA_UI_CONTEXT_MENU_WIDGET_QT_H_

#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include <vector>

namespace xe {
namespace hid {
class InputSystem;
}  // namespace hid
}  // namespace xe

namespace xe {
namespace app {

// Context menu widget that works like the toast - as a child widget overlay
// instead of a separate window to avoid Windows compositor issues
class ContextMenuWidgetQt : public QWidget {
  Q_OBJECT

 public:
  ContextMenuWidgetQt(QWidget* parent,
                      hid::InputSystem* input_system = nullptr);
  ~ContextMenuWidgetQt() override;

  void AddAction(const QString& text, std::function<void()> callback,
                 const QString& shortcut = QString());
  void AddSeparator();
  void ShowAt(const QPoint& global_pos);

 protected:
  void keyPressEvent(QKeyEvent* event) override;
  bool eventFilter(QObject* obj, QEvent* event) override;

 private slots:
  void PollGamepad();

 private:
  void UpdateFocusedItem(int index);
  void ActivateFocusedItem();

  QVBoxLayout* layout_;

  // Gamepad support
  hid::InputSystem* input_system_;
  QTimer* poll_timer_;
  std::vector<std::pair<QWidget*, std::function<void()>>> menu_items_;
  int focused_index_;
  uint16_t prev_buttons_;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_UI_CONTEXT_MENU_WIDGET_QT_H_
