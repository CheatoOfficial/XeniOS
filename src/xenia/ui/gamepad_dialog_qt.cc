/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/gamepad_dialog_qt.h"

#include <cstdlib>

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidget>

#include "xenia/base/logging.h"
#include "xenia/hid/input_system.h"

namespace xe {
namespace ui {

GamepadDialog::GamepadDialog(QWidget* parent, hid::InputSystem* input_system)
    : QDialog(parent),
      input_system_(input_system),
      poll_timer_(nullptr),
      current_focus_index_(-1),
      prev_buttons_(0),
      repeat_counter_(0),
      scroll_accumulator_(0.0f) {
  if (input_system_) {
    // Block input to the game while this dialog is open
    input_system_->AddUIInputBlocker();

    // Initialize prev_buttons_ to current state so held buttons aren't
    // detected as "just pressed" when the dialog opens
    hid::X_INPUT_STATE state;
    for (uint32_t user_index = 0; user_index < 4; user_index++) {
      if (input_system_->GetStateForUI(user_index, 1, &state) == 0) {
        prev_buttons_ = state.gamepad.buttons;
        break;
      }
    }

    poll_timer_ = new QTimer(this);
    connect(poll_timer_, &QTimer::timeout, this, &GamepadDialog::PollGamepad);
    poll_timer_->start(16);  // Poll at ~60Hz
  }
}

GamepadDialog::~GamepadDialog() {
  if (poll_timer_) {
    poll_timer_->stop();
    delete poll_timer_;
  }
  if (input_system_) {
    // Unblock input to the game
    input_system_->RemoveUIInputBlocker();
  }
}

void GamepadDialog::PollGamepad() {
  if (!input_system_) {
    return;
  }

  // Try to get input from any connected controller
  hid::X_INPUT_STATE state;
  bool got_input = false;
  for (uint32_t user_index = 0; user_index < 4; user_index++) {
    // Pass InputType::Controller (1) as flags to get controller input
    // Use GetStateForUI to bypass the input blocker (we ARE the UI)
    if (input_system_->GetStateForUI(user_index, 1, &state) == 0) {
      got_input = true;
      break;
    }
  }

  if (!got_input) {
    repeat_counter_ = 0;
    prev_buttons_ = 0;
    return;
  }

  uint16_t buttons = state.gamepad.buttons;
  uint16_t pressed = buttons & ~prev_buttons_;  // Edge detection

  // Button mapping (no repeat for these)
  if (pressed & 0x1000) {  // A button
    OnGamepadButtonA();
  }
  if (pressed & 0x2000) {  // B button
    OnGamepadButtonB();
  }
  if (pressed & 0x4000) {  // X button
    OnGamepadButtonX();
  }
  if (pressed & 0x8000) {  // Y button
    OnGamepadButtonY();
  }
  if (pressed & 0x0010) {  // Start button
    OnGamepadStart();
  }
  if (pressed & 0x0020) {  // Back button
    OnGamepadBack();
  }
  if (pressed & 0x0400) {  // Guide button
    OnGamepadGuide();
  }

  // D-pad navigation with repeat
  // First press (edge detection)
  bool navigated = false;
  if (pressed & 0x0001) {  // D-pad Up
    NavigateFocusVertical(-1);
    navigated = true;
  }
  if (pressed & 0x0002) {  // D-pad Down
    NavigateFocusVertical(1);
    navigated = true;
  }
  if (pressed & 0x0004) {  // D-pad Left
    NavigateFocusHorizontal(-1);
    navigated = true;
  }
  if (pressed & 0x0008) {  // D-pad Right
    NavigateFocusHorizontal(1);
    navigated = true;
  }

  // Handle button repeat for held D-pad
  const uint16_t dpad_mask = 0x000F;  // All D-pad buttons
  if (buttons & dpad_mask) {
    repeat_counter_++;
    // Initial delay: 30 polls (~500ms), then repeat every 4 polls (~67ms)
    const int initial_delay = 30;
    const int repeat_rate = 4;

    if (repeat_counter_ >= initial_delay &&
        (repeat_counter_ - initial_delay) % repeat_rate == 0) {
      if (buttons & 0x0001) NavigateFocusVertical(-1);
      if (buttons & 0x0002) NavigateFocusVertical(1);
      if (buttons & 0x0004) NavigateFocusHorizontal(-1);
      if (buttons & 0x0008) NavigateFocusHorizontal(1);
      navigated = true;
    }
  } else {
    repeat_counter_ = 0;
  }

  // Right stick scrolling (moves scrollbar without changing selection)
  const int16_t deadzone = 7849;  // ~30% deadzone
  int16_t right_y = state.gamepad.thumb_ry;

  if (abs(right_y) > deadzone && current_focus_index_ >= 0 &&
      current_focus_index_ < static_cast<int>(focusable_widgets_.size())) {
    auto* widget = focusable_widgets_[current_focus_index_];

    // Find the scrollable area
    QAbstractScrollArea* scroll_area =
        qobject_cast<QAbstractScrollArea*>(widget);
    if (!scroll_area) {
      // Check if it's a child of a scroll area
      QWidget* parent = widget->parentWidget();
      while (parent && !scroll_area) {
        scroll_area = qobject_cast<QAbstractScrollArea*>(parent);
        parent = parent->parentWidget();
      }
    }

    if (scroll_area && scroll_area->verticalScrollBar()) {
      auto* scrollbar = scroll_area->verticalScrollBar();
      // Invert Y axis (up is positive in gamepad coords)
      // Accumulate fractional scrolling for smooth movement
      scroll_accumulator_ += -static_cast<float>(right_y) / 50000.0f;

      int scroll_pixels = static_cast<int>(scroll_accumulator_);
      if (scroll_pixels != 0) {
        scrollbar->setValue(scrollbar->value() + scroll_pixels);
        scroll_accumulator_ -= scroll_pixels;  // Keep fractional part
      }
    }
  } else {
    scroll_accumulator_ = 0.0f;  // Reset when stick released
  }

  prev_buttons_ = buttons;
}

void GamepadDialog::showEvent(QShowEvent* event) {
  QDialog::showEvent(event);
  UpdateFocusableWidgets();

  // Auto-focus first widget
  if (!focusable_widgets_.empty()) {
    current_focus_index_ = 0;
    focusable_widgets_[0]->setFocus();
    ApplyFocusStyle(focusable_widgets_[0], true);
  }
}

void GamepadDialog::UpdateFocusableWidgets() {
  focusable_widgets_.clear();

  // Find all focusable widgets in the dialog
  auto all_widgets = findChildren<QWidget*>();
  for (auto* widget : all_widgets) {
    if (IsWidgetGamepadFocusable(widget)) {
      focusable_widgets_.push_back(widget);
    }
  }
}

bool GamepadDialog::IsWidgetGamepadFocusable(QWidget* widget) const {
  if (!widget || !widget->isVisible() || !widget->isEnabled()) {
    return false;
  }

  // Check if it's a focusable widget type
  return qobject_cast<QPushButton*>(widget) ||
         qobject_cast<QAbstractButton*>(widget) ||
         qobject_cast<QLineEdit*>(widget) || qobject_cast<QComboBox*>(widget) ||
         qobject_cast<QSpinBox*>(widget) || qobject_cast<QSlider*>(widget) ||
         qobject_cast<QListWidget*>(widget) ||
         qobject_cast<QTableWidget*>(widget);
}

void GamepadDialog::NavigateFocusVertical(int direction) {
  if (focusable_widgets_.empty()) {
    return;
  }

  // Check if current focused widget is a list/table - navigate items within it
  bool navigate_items = false;
  if (current_focus_index_ >= 0 &&
      current_focus_index_ < static_cast<int>(focusable_widgets_.size())) {
    auto* widget = focusable_widgets_[current_focus_index_];

    // Handle QListWidget - navigate items
    if (auto* list = qobject_cast<QListWidget*>(widget)) {
      int current_row = list->currentRow();
      int new_row = current_row + direction;
      if (new_row >= 0 && new_row < list->count()) {
        list->setCurrentRow(new_row);
        list->scrollToItem(list->currentItem());
        return;  // Successfully navigated within list
      }
      navigate_items = true;
    }

    // Handle QTableWidget - navigate rows
    if (auto* table = qobject_cast<QTableWidget*>(widget)) {
      int current_row = table->currentRow();
      int new_row = current_row + direction;
      if (new_row >= 0 && new_row < table->rowCount()) {
        table->setCurrentCell(new_row, table->currentColumn());
        table->scrollToItem(table->currentItem());
        return;  // Successfully navigated within table
      }
      navigate_items = true;
    }
  }

  // If we're at the edge of a list/table, or not in one, navigate between
  // widgets Clear old focus style
  if (current_focus_index_ >= 0 &&
      current_focus_index_ < static_cast<int>(focusable_widgets_.size())) {
    ApplyFocusStyle(focusable_widgets_[current_focus_index_], false);
  }

  // Navigate
  current_focus_index_ += direction;
  if (current_focus_index_ < 0) {
    current_focus_index_ = static_cast<int>(focusable_widgets_.size()) - 1;
  } else if (current_focus_index_ >=
             static_cast<int>(focusable_widgets_.size())) {
    current_focus_index_ = 0;
  }

  // Apply new focus
  auto* widget = focusable_widgets_[current_focus_index_];
  widget->setFocus();
  ApplyFocusStyle(widget, true);

  // Scroll into view if needed
  if (auto* scroll_area = qobject_cast<QScrollArea*>(widget->parentWidget())) {
    scroll_area->ensureWidgetVisible(widget);
  }
}

void GamepadDialog::NavigateFocusHorizontal(int direction) {
  if (focusable_widgets_.empty()) {
    return;
  }

  // Check if current focused widget is a slider - adjust value
  if (current_focus_index_ >= 0 &&
      current_focus_index_ < static_cast<int>(focusable_widgets_.size())) {
    auto* widget = focusable_widgets_[current_focus_index_];

    if (auto* slider = qobject_cast<QSlider*>(widget)) {
      // Use pageStep for controller (larger increments), fallback to 5% of
      // range
      int step = slider->pageStep();
      if (step <= 1) {
        step = qMax(1, (slider->maximum() - slider->minimum()) / 20);  // 5%
      }
      int new_value = slider->value() + (direction * step);
      new_value = qBound(slider->minimum(), new_value, slider->maximum());
      slider->setValue(new_value);
      return;
    }
  }

  // For non-slider widgets, horizontal navigation moves between widgets
  NavigateFocusVertical(direction);
}

void GamepadDialog::AcceptFocusedButton() {
  if (current_focus_index_ < 0 ||
      current_focus_index_ >= static_cast<int>(focusable_widgets_.size())) {
    return;
  }

  auto* widget = focusable_widgets_[current_focus_index_];
  if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
    button->click();
  }
}

void GamepadDialog::ApplyFocusStyle(QWidget* widget, bool focused) {
  if (!widget) {
    return;
  }

  // Don't apply border to list/table widgets - they have their own item
  // selection highlighting
  if (qobject_cast<QListWidget*>(widget) ||
      qobject_cast<QTableWidget*>(widget)) {
    return;
  }

  if (focused) {
    // Save original stylesheet for this specific widget
    if (!original_stylesheets_.contains(widget)) {
      original_stylesheets_[widget] = widget->styleSheet();
    }

    // Use a dynamic property to indicate focus, which can be styled
    // This approach preserves the widget's existing styles
    widget->setProperty("gamepad_focused", true);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);

    // Apply focus border using the widget's specific type for better
    // specificity
    QString original = original_stylesheets_[widget];
    QString focus_style;
    if (qobject_cast<QPushButton*>(widget)) {
      focus_style =
          original + "\nQPushButton { border: 2px solid #0078d7 !important; }";
    } else if (qobject_cast<QSlider*>(widget)) {
      focus_style =
          original +
          "\nQSlider { border: 2px solid #0078d7; border-radius: 4px; }";
    } else {
      focus_style =
          original + "\n* { border: 2px solid #0078d7; border-radius: 4px; }";
    }
    widget->setStyleSheet(focus_style);
  } else {
    // Restore original stylesheet for this widget
    widget->setProperty("gamepad_focused", false);
    if (original_stylesheets_.contains(widget)) {
      widget->setStyleSheet(original_stylesheets_[widget]);
    }
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
  }
}

bool GamepadDialog::eventFilter(QObject* obj, QEvent* event) {
  // Let the base class handle the event
  return QDialog::eventFilter(obj, event);
}

}  // namespace ui
}  // namespace xe
