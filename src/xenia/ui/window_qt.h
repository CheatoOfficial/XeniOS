/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOW_QT_H_
#define XENIA_UI_WINDOW_QT_H_

#include <memory>
#include <string>

#include <QMainWindow>
#include <QWidget>
#include <QWindow>

#include "xenia/ui/menu_item.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {

class QtWindowedAppContext;

class QtWindowInternal;
class GameQWindow;

class QtWindow : public Window {
  using super = Window;

 public:
  QtWindow(WindowedAppContext& app_context, const std::string_view title,
           uint32_t desired_logical_width, uint32_t desired_logical_height,
           bool is_game_process = false);
  ~QtWindow() override;

  // UI process window (QMainWindow with menu bar)
  QMainWindow* qwindow() const { return qwindow_; }
  // Game process window (native QWindow)
  GameQWindow* game_window() const { return game_qwindow_; }

  void TriggerPaint() { OnPaint(); }

  // Public accessors for event handling from GameQWindow
  void OnMouseMoveEvent(QMouseEvent* event);
  void OnMousePressEvent(QMouseEvent* event);
  void OnMouseReleaseEvent(QMouseEvent* event);
  void OnMouseDoubleClickEvent(QMouseEvent* event);
  void OnWheelEvent(QWheelEvent* event);

 protected:
  bool OpenImpl() override;
  void RequestCloseImpl() override;

  void ApplyNewFullscreen() override;
  void ApplyNewTitle() override;
  void LoadAndApplyIcon(const void* buffer, size_t size,
                        bool can_apply_state_in_current_phase) override;
  void ApplyNewMainMenu(MenuItem* old_main_menu) override;
  void ApplyNewCursorVisibility(
      CursorVisibility old_cursor_visibility) override;
  void FocusImpl() override;

  std::unique_ptr<Surface> CreateSurfaceImpl(
      Surface::TypeFlags allowed_types) override;
  void RequestPaintImpl() override;

 private:
  friend class QtWindowInternal;
  friend class GameQWindow;

  // Separate open implementations for UI vs game process
  bool OpenUIWindow();
  bool OpenGameWindow();

  void HandleSizeUpdate(WindowDestructionReceiver& destruction_receiver);
  static VirtualKey TranslateVirtualKey(int qt_key);

  void OnCloseEvent(QCloseEvent* event);
  void OnResizeEvent(QResizeEvent* event);
  void OnFocusInEvent(QFocusEvent* event);
  void OnFocusOutEvent(QFocusEvent* event);
  void OnKeyPressEvent(QKeyEvent* event);
  void OnKeyReleaseEvent(QKeyEvent* event);
  bool OnEventFilter(QObject* obj, QEvent* event);

  void SetCursorAutoHideTimer();
  void OnCursorAutoHideTimeout();
  void UpdateCursorVisibility();

  // UI process window (QMainWindow with menu bar, pure Qt widgets)
  QMainWindow* qwindow_ = nullptr;

  // Game process window (native QWindow for rendering)
  GameQWindow* game_qwindow_ = nullptr;

  bool in_size_update_ = false;
  bool is_game_process_ = false;

  // Cursor auto-hide support
  QTimer* cursor_auto_hide_timer_ = nullptr;
  QPoint cursor_auto_hide_last_pos_;
  bool cursor_currently_auto_hidden_ = false;
};

class QtMenuItem : public MenuItem {
 public:
  QtMenuItem(Type type, const std::string& text, const std::string& hotkey,
             std::function<void()> callback);
  ~QtMenuItem() override;

  QAction* action() const { return action_; }
  QMenu* menu() const { return menu_; }

 protected:
  void OnChildAdded(MenuItem* child_item) override;
  void OnChildRemoved(MenuItem* child_item) override;

 private:
  void OnTriggered();

  QAction* action_ = nullptr;
  QMenu* menu_ = nullptr;
};

// Native QWindow for game process rendering (similar to RPCS3's gs_frame)
// Used instead of QWidget to avoid Qt painting system overhead
class GameQWindow : public QWindow {
 public:
  explicit GameQWindow(QtWindow* window);

 protected:
  void exposeEvent(QExposeEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  bool event(QEvent* event) override;

 private:
  QtWindow* window_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOW_QT_H_
