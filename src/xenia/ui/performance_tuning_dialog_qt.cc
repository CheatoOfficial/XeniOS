/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/performance_tuning_dialog_qt.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStyle>

#include "xenia/app/emulator_window.h"
#include "xenia/base/cvar.h"
#include "xenia/config.h"
#include "xenia/emulator.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/ui/notification_widget_qt.h"

DECLARE_bool(clear_memory_page_state);
DECLARE_bool(readback_memexport);
DECLARE_bool(readback_memexport_fast);
DECLARE_string(readback_resolve);
DECLARE_bool(vsync);
DECLARE_bool(occlusion_query_enable);

namespace xe {
namespace app {

PerformanceTuningDialogQt::PerformanceTuningDialogQt(
    QWidget* parent, EmulatorWindow* emulator_window,
    hid::InputSystem* input_system)
    : ui::GamepadDialog(parent, input_system),
      emulator_window_(emulator_window) {
  SetupUI();
  LoadCurrentSettings();

  // Position near top, centered horizontally
  if (parent) {
    QPoint parent_pos = parent->mapToGlobal(QPoint(0, 0));
    int center_x = parent_pos.x() + (parent->width() - width()) / 2;
    move(center_x, parent_pos.y() + 20);
  }
}

PerformanceTuningDialogQt::~PerformanceTuningDialogQt() = default;

void PerformanceTuningDialogQt::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    // Only start drag if clicking directly on the dialog background
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

void PerformanceTuningDialogQt::mouseMoveEvent(QMouseEvent* event) {
  if (dragging_ && (event->buttons() & Qt::LeftButton)) {
    move(event->globalPosition().toPoint() - drag_position_);
    event->accept();
    return;
  }
  QDialog::mouseMoveEvent(event);
}

void PerformanceTuningDialogQt::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    dragging_ = false;
  }
  QDialog::mouseReleaseEvent(event);
}

void PerformanceTuningDialogQt::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_F7) {
    close();
    return;
  }
  QDialog::keyPressEvent(event);
}

void PerformanceTuningDialogQt::SetupUI() {
  setWindowTitle("Performance Settings");
  setModal(false);
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
  setMinimumWidth(500);

  // Set window opacity (0.92 = 92% opaque)
  setWindowOpacity(0.92);

  // Set semi-transparent background similar to ImGui
  setStyleSheet(R"(
    QDialog {
      background-color: rgb(30, 30, 30);
      border: 1px solid rgba(100, 100, 100, 180);
      padding: 0px;
    }
    QGroupBox {
      background-color: rgba(40, 40, 40, 200);
      border: 1px solid rgba(80, 80, 80, 150);
      margin-top: 12px;
      padding-top: 16px;
      font-weight: bold;
      color: #e0e0e0;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      subcontrol-position: top left;
      padding: 4px 8px;
      color: #f0f0f0;
    }
    QLabel {
      color: #d0d0d0;
      background-color: transparent;
    }
    QCheckBox {
      color: #d0d0d0;
    }
    QCheckBox::indicator {
      width: 16px;
      height: 16px;
      border-radius: 3px;
      border: 2px solid #909090;
      background-color: rgba(50, 50, 50, 200);
    }
    QCheckBox::indicator:hover {
      border-color: #b0b0b0;
      background-color: rgba(70, 70, 70, 200);
    }
    QCheckBox::indicator:checked {
      border-color: #107c10;
      background-color: #107c10;
    }
    QRadioButton {
      color: #d0d0d0;
    }
    QRadioButton::indicator {
      width: 14px;
      height: 14px;
      border-radius: 7px;
      border: 2px solid #909090;
      background-color: rgba(50, 50, 50, 200);
    }
    QRadioButton::indicator:hover {
      border-color: #b0b0b0;
      background-color: rgba(70, 70, 70, 200);
    }
    QRadioButton::indicator:checked {
      border-color: #107c10;
      background-color: #107c10;
    }
  )");

  auto* content_layout = new QVBoxLayout(this);
  content_layout->setContentsMargins(16, 16, 16, 16);
  content_layout->setSpacing(12);

  // Top bar with title and close button
  auto* top_bar_layout = new QHBoxLayout();
  auto* title_label = new QLabel("Performance Settings", this);
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

  // Readback Resolve group
  readback_resolve_group_ = new QGroupBox("Readback Resolve", this);
  auto* resolve_layout = new QHBoxLayout(readback_resolve_group_);
  resolve_layout->setSpacing(16);

  readback_resolve_button_group_ = new QButtonGroup(this);
  const char* resolve_labels[] = {"None", "Some", "Fast", "Full"};
  for (int i = 0; i < 4; i++) {
    auto* radio = new QRadioButton(resolve_labels[i], readback_resolve_group_);
    readback_resolve_button_group_->addButton(radio, i);
    resolve_layout->addWidget(radio);
  }
  resolve_layout->addStretch();

  connect(readback_resolve_button_group_, &QButtonGroup::idClicked, this,
          &PerformanceTuningDialogQt::OnReadbackResolveChanged);

  content_layout->addWidget(readback_resolve_group_);

  // Readback Memexport group
  readback_memexport_group_ = new QGroupBox("Readback Memexport", this);
  auto* memexport_layout = new QHBoxLayout(readback_memexport_group_);
  memexport_layout->setSpacing(16);

  readback_memexport_button_group_ = new QButtonGroup(this);
  const char* memexport_labels[] = {"None", "Fast", "Full"};
  for (int i = 0; i < 3; i++) {
    auto* radio =
        new QRadioButton(memexport_labels[i], readback_memexport_group_);
    readback_memexport_button_group_->addButton(radio, i);
    memexport_layout->addWidget(radio);
  }
  memexport_layout->addStretch();

  connect(readback_memexport_button_group_, &QButtonGroup::idClicked, this,
          &PerformanceTuningDialogQt::OnReadbackMemexportChanged);

  content_layout->addWidget(readback_memexport_group_);

  // Other settings group
  other_group_ = new QGroupBox("Other", this);
  auto* other_layout = new QVBoxLayout(other_group_);
  other_layout->setSpacing(6);

  vsync_checkbox_ = new QCheckBox("Enable VSync", other_group_);
  other_layout->addWidget(vsync_checkbox_);
  connect(vsync_checkbox_, &QCheckBox::checkStateChanged, this,
          &PerformanceTuningDialogQt::OnVsyncChanged);

  occlusion_query_checkbox_ =
      new QCheckBox("Enable hardware occlusion queries", other_group_);
  other_layout->addWidget(occlusion_query_checkbox_);
  connect(occlusion_query_checkbox_, &QCheckBox::checkStateChanged, this,
          &PerformanceTuningDialogQt::OnOcclusionQueryChanged);

  clear_memory_checkbox_ = new QCheckBox(
      "Clear memory page state on GPU cache invalidation", other_group_);
  other_layout->addWidget(clear_memory_checkbox_);
  connect(clear_memory_checkbox_, &QCheckBox::checkStateChanged, this,
          &PerformanceTuningDialogQt::OnClearMemoryPageStateChanged);

  content_layout->addWidget(other_group_);

  content_layout->addStretch();
}

void PerformanceTuningDialogQt::LoadCurrentSettings() {
  // Block signals to prevent notifications during initial load
  vsync_checkbox_->blockSignals(true);
  occlusion_query_checkbox_->blockSignals(true);
  readback_resolve_button_group_->blockSignals(true);
  readback_memexport_button_group_->blockSignals(true);
  clear_memory_checkbox_->blockSignals(true);

  // Load VSync setting
  vsync_checkbox_->setChecked(cvars::vsync);

  // Load Occlusion Query setting
  occlusion_query_checkbox_->setChecked(cvars::occlusion_query_enable);

  // Load Readback Resolve setting (0=none, 1=some, 2=fast, 3=full)
  const std::string& resolve_mode = cvars::readback_resolve;
  int resolve_index = 2;  // Default to "fast"
  if (resolve_mode == "none") {
    resolve_index = 0;
  } else if (resolve_mode == "some") {
    resolve_index = 1;
  } else if (resolve_mode == "full") {
    resolve_index = 3;
  }
  if (auto* button = readback_resolve_button_group_->button(resolve_index)) {
    button->setChecked(true);
  }

  // Load Readback Memexport setting (0=none, 1=fast, 2=full)
  int memexport_index = 1;  // Default to "fast"
  if (!cvars::readback_memexport) {
    memexport_index = 0;
  } else if (!cvars::readback_memexport_fast) {
    memexport_index = 2;
  }
  if (auto* button =
          readback_memexport_button_group_->button(memexport_index)) {
    button->setChecked(true);
  }

  // Load Clear Memory Page State setting
  clear_memory_checkbox_->setChecked(cvars::clear_memory_page_state);

  // Restore signals
  vsync_checkbox_->blockSignals(false);
  occlusion_query_checkbox_->blockSignals(false);
  readback_resolve_button_group_->blockSignals(false);
  readback_memexport_button_group_->blockSignals(false);
  clear_memory_checkbox_->blockSignals(false);
}

void PerformanceTuningDialogQt::ShowNotification(const QString& title,
                                                 const QString& description) {
  auto* notification =
      new NotificationWidgetQt(parentWidget(), title, description, 2000);
  notification->Show();
}

void PerformanceTuningDialogQt::OnVsyncChanged(int state) {
  bool enabled = (state == Qt::Checked);
  SetVsync(enabled);
  config::SaveGameConfigSetting(emulator_window_->emulator(), "GPU", "vsync",
                                enabled);
  ShowNotification("VSync", enabled ? "Enabled" : "Disabled");
}

void PerformanceTuningDialogQt::OnOcclusionQueryChanged(int state) {
  bool enabled = (state == Qt::Checked);
  SetOcclusionQueryEnable(enabled);
  config::SaveGameConfigSetting(emulator_window_->emulator(), "GPU",
                                "occlusion_query_enable", enabled);
  ShowNotification("Occlusion Queries", enabled ? "Enabled" : "Disabled");
}

void PerformanceTuningDialogQt::OnReadbackResolveChanged(int value) {
  auto emulator = emulator_window_->emulator();
  if (!emulator) return;

  auto graphics_system = emulator->graphics_system();
  if (!graphics_system) return;

  auto command_processor = graphics_system->command_processor();
  if (!command_processor) return;

  // Slider: 0=none, 1=some, 2=fast, 3=full
  gpu::ReadbackResolveMode mode;
  switch (value) {
    case 0:
      mode = gpu::ReadbackResolveMode::kDisabled;
      break;
    case 1:
      mode = gpu::ReadbackResolveMode::kSome;
      break;
    case 3:
      mode = gpu::ReadbackResolveMode::kFull;
      break;
    default:
      mode = gpu::ReadbackResolveMode::kFast;
      break;
  }

  command_processor->SetReadbackResolveMode(mode);

  const char* mode_names[] = {"None", "Some", "Fast", "Full"};
  ShowNotification("Readback Resolve", mode_names[value]);
}

void PerformanceTuningDialogQt::OnReadbackMemexportChanged(int value) {
  // Slider: 0=none, 1=fast, 2=full
  bool memexport_enabled = true;
  bool memexport_fast = true;

  switch (value) {
    case 0:  // None
      memexport_enabled = false;
      break;
    case 1:  // Fast
      memexport_fast = true;
      break;
    case 2:  // Full
      memexport_fast = false;
      break;
  }

  gpu::SaveGPUSetting(gpu::GPUSetting::ReadbackMemexport, memexport_enabled);
  gpu::SaveGPUSetting(gpu::GPUSetting::ReadbackMemexportFast, memexport_fast);
  config::SaveGameConfigSetting(emulator_window_->emulator(), "GPU",
                                "readback_memexport", memexport_enabled);
  config::SaveGameConfigSetting(emulator_window_->emulator(), "GPU",
                                "readback_memexport_fast", memexport_fast);

  const char* mode_names[] = {"None", "Fast", "Full"};
  ShowNotification("Readback Memexport", mode_names[value]);
}

void PerformanceTuningDialogQt::OnClearMemoryPageStateChanged(int state) {
  bool enabled = (state == Qt::Checked);
  gpu::SaveGPUSetting(gpu::GPUSetting::ClearMemoryPageState, enabled);
  config::SaveGameConfigSetting(emulator_window_->emulator(), "GPU",
                                "clear_memory_page_state", enabled);
  ShowNotification("Clear Memory Page State", enabled ? "Enabled" : "Disabled");
}

}  // namespace app
}  // namespace xe
