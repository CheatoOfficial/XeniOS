/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/context_menu_widget_qt.h"

#include <QFrame>
#include <QKeyEvent>
#include <QMouseEvent>

namespace xe {
namespace app {

ContextMenuWidgetQt::ContextMenuWidgetQt(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_DeleteOnClose);
  setAutoFillBackground(true);
  setAttribute(Qt::WA_StyledBackground, true);

  setStyleSheet(
      "background-color: rgb(45, 45, 45); "
      "border: 1px solid rgb(85, 85, 85);");

  layout_ = new QVBoxLayout(this);
  layout_->setContentsMargins(2, 2, 2, 2);
  layout_->setSpacing(0);

  // Install global event filter to close menu on clicks outside
  qApp->installEventFilter(this);
}

void ContextMenuWidgetQt::AddAction(const QString& text,
                                    std::function<void()> callback) {
  // Make the label clickable
  class ClickableLabel : public QLabel {
   public:
    ClickableLabel(const QString& text, QWidget* parent,
                   std::function<void()> cb)
        : QLabel(text, parent), callback_(cb) {
      setStyleSheet(
          "QLabel { padding: 5px 30px; color: white; background: transparent; "
          "}");
    }

   protected:
    void mousePressEvent(QMouseEvent* event) override {
      if (event->button() == Qt::LeftButton && callback_) {
        callback_();
        // Close the menu
        if (parentWidget()) {
          parentWidget()->close();
        }
      }
    }

    void enterEvent(QEnterEvent* event) override {
      setStyleSheet(
          "QLabel { padding: 5px 30px; color: white; background-color: rgb(74, "
          "74, 74); }");
    }

    void leaveEvent(QEvent* event) override {
      setStyleSheet(
          "QLabel { padding: 5px 30px; color: white; background: transparent; "
          "}");
    }

   private:
    std::function<void()> callback_;
  };

  auto* clickable = new ClickableLabel(text, this, callback);
  layout_->addWidget(clickable);
}

void ContextMenuWidgetQt::AddSeparator() {
  auto* separator = new QFrame(this);
  separator->setFrameShape(QFrame::HLine);
  separator->setStyleSheet("QFrame { background-color: rgb(85, 85, 85); }");
  separator->setFixedHeight(1);
  layout_->addWidget(separator);
}

void ContextMenuWidgetQt::ShowAt(const QPoint& global_pos) {
  if (!parentWidget()) {
    return;
  }

  adjustSize();

  // Convert global position to parent widget coordinates
  QPoint local_pos = parentWidget()->mapFromGlobal(global_pos);

  // Adjust if menu would go off screen
  QWidget* parent = parentWidget();
  int x = local_pos.x();
  int y = local_pos.y();

  if (x + width() > parent->width()) {
    x = parent->width() - width();
  }
  if (y + height() > parent->height()) {
    y = parent->height() - height();
  }

  move(x, y);

  show();
  raise();
  setFocus(Qt::PopupFocusReason);
}

void ContextMenuWidgetQt::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    close();
  }
  QWidget::keyPressEvent(event);
}

bool ContextMenuWidgetQt::eventFilter(QObject* obj, QEvent* event) {
  if (event->type() == QEvent::MouseButtonPress) {
    QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

    if (mouseEvent->button() == Qt::LeftButton) {
      // Check if click is outside this menu widget
      QWidget* widget = qobject_cast<QWidget*>(obj);
      if (widget) {
        QPoint globalPos = widget->mapToGlobal(mouseEvent->pos());
        QPoint localPos = mapFromGlobal(globalPos);

        if (!rect().contains(localPos)) {
          // Click outside menu - close it and consume the event
          close();
          return true;
        }
      }
    }
  }
  return false;
}

}  // namespace app
}  // namespace xe
