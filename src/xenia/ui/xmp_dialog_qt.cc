/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/xmp_dialog_qt.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QStyle>
#include <QVBoxLayout>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/app/emulator_window.h"
#include "xenia/ui/qt_util.h"

namespace xe {
namespace app {

using xe::ui::SafeQString;

XmpDialogQt::XmpDialogQt(QWidget* parent, EmulatorWindow* emulator_window,
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

XmpDialogQt::~XmpDialogQt() = default;

void XmpDialogQt::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    drag_position_ = event->position().toPoint();
    event->accept();
  }
}

void XmpDialogQt::mouseMoveEvent(QMouseEvent* event) {
  if (event->buttons() & Qt::LeftButton) {
    QPoint global_pos = event->globalPosition().toPoint();
    move(global_pos - drag_position_);
    event->accept();
  }
}

void XmpDialogQt::showEvent(QShowEvent* event) {
  GamepadDialog::showEvent(event);
  UpdatePlayerState();
}

void XmpDialogQt::SetupUI() {
  setWindowTitle("Audio Player Menu");
  setModal(false);
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
  setMinimumWidth(400);

  // Set window opacity (0.92 = 92% opaque) - same as post-processing dialog
  setWindowOpacity(0.92);

  // Set semi-transparent background similar to post-processing dialog
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
    QPushButton {
      background-color: rgba(60, 60, 60, 200);
      border: 1px solid rgba(100, 100, 100, 150);
      border-radius: 4px;
      color: #e0e0e0;
      padding: 8px 16px;
      font-weight: bold;
    }
    QPushButton:hover {
      background-color: rgba(80, 80, 80, 220);
      border-color: #b0b0b0;
    }
    QPushButton:pressed {
      background-color: rgba(50, 50, 50, 240);
      border-color: #707070;
    }
    QSlider::groove:horizontal {
      border: 1px solid rgba(100, 100, 100, 150);
      height: 8px;
      background: rgba(50, 50, 50, 200);
      margin: 2px 0;
      border-radius: 4px;
    }
    QSlider::handle:horizontal {
      background: #107c10;
      border: 1px solid rgba(100, 100, 100, 150);
      width: 18px;
      margin: -5px 0;
      border-radius: 9px;
    }
    QSlider::handle:horizontal:hover {
      background: #138f13;
      border-color: #b0b0b0;
    }
    QSlider::sub-page:horizontal {
      background: #107c10;
      border: 1px solid rgba(100, 100, 100, 150);
      height: 8px;
      border-radius: 4px;
    }
  )");

  auto* content_layout = new QVBoxLayout(this);
  content_layout->setContentsMargins(16, 16, 16, 16);
  content_layout->setSpacing(12);

  // Top bar with close button
  auto* top_bar_layout = new QHBoxLayout();
  auto* title_label = new QLabel("XMP Audio Player", this);
  title_label->setStyleSheet("font-weight: bold; font-size: 14px;");
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
  content_layout->addSpacing(6);

  // Status display
  auto* status_layout = new QHBoxLayout();
  status_label_ = new QLabel("Audio player status:", this);
  status_value_label_ = new QLabel("Idle", this);
  status_value_label_->setStyleSheet("color: #f0f0f0; font-weight: bold;");
  status_layout->addWidget(status_label_);
  status_layout->addWidget(status_value_label_);
  status_layout->addStretch();
  content_layout->addLayout(status_layout);

  content_layout->addSpacing(8);

  // Play/Pause button
  play_pause_button_ = new QPushButton("Resume", this);
  play_pause_button_->setMinimumHeight(36);
  connect(play_pause_button_, &QPushButton::clicked, this,
          &XmpDialogQt::OnPlayPauseClicked);
  content_layout->addWidget(play_pause_button_);

  content_layout->addSpacing(8);

  // Volume control
  volume_label_ = new QLabel("Audio player volume:", this);
  content_layout->addWidget(volume_label_);

  auto* volume_layout = new QHBoxLayout();
  volume_slider_ = new QSlider(Qt::Horizontal, this);
  volume_slider_->setRange(0, 100);  // Will be mapped to 0.0-1.0
  volume_slider_->setValue(0);
  volume_value_label_ = new QLabel("0%", this);
  volume_value_label_->setMinimumWidth(50);
  volume_value_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

  volume_layout->addWidget(volume_slider_);
  volume_layout->addWidget(volume_value_label_);
  content_layout->addLayout(volume_layout);

  connect(volume_slider_, &QSlider::valueChanged, this,
          &XmpDialogQt::OnVolumeChanged);

  content_layout->addStretch();
}

void XmpDialogQt::UpdatePlayerState() {
  auto emulator = emulator_window_->emulator();
  if (!emulator) {
    return;
  }

  auto audio_player = emulator->audio_media_player();
  if (!audio_player) {
    return;
  }

  using xmp_state = kernel::xam::apps::XmpApp::State;

  // Update status label
  switch (audio_player->GetState()) {
    case xmp_state::kIdle:
      status_value_label_->setText("Idle");
      break;
    case xmp_state::kPaused:
      status_value_label_->setText("Paused");
      break;
    case xmp_state::kPlaying:
      status_value_label_->setText("Playing");
      break;
    default:
      status_value_label_->setText("Unknown");
      break;
  }

  // Update play/pause button
  if (audio_player->IsPlaying()) {
    play_pause_button_->setText("Pause");
    play_pause_button_->setEnabled(true);
  } else if (audio_player->IsPaused()) {
    play_pause_button_->setText("Resume");
    play_pause_button_->setEnabled(true);
  } else {
    play_pause_button_->setText("Resume");
    play_pause_button_->setEnabled(false);
  }

  // Update volume slider
  float volume = audio_player->GetVolume()->load();
  volume_slider_->blockSignals(true);
  volume_slider_->setValue(static_cast<int>(volume * 100));
  volume_value_label_->setText(
      SafeQString(fmt::format("{}%", static_cast<int>(volume * 100))));
  volume_slider_->blockSignals(false);
}

void XmpDialogQt::OnPlayPauseClicked() {
  auto emulator = emulator_window_->emulator();
  if (!emulator) {
    return;
  }

  auto audio_player = emulator->audio_media_player();
  if (!audio_player) {
    return;
  }

  if (audio_player->IsPlaying()) {
    audio_player->Pause();
  } else if (audio_player->IsPaused()) {
    audio_player->Continue();
  }

  UpdatePlayerState();
}

void XmpDialogQt::OnVolumeChanged(int value) {
  auto emulator = emulator_window_->emulator();
  if (!emulator) {
    return;
  }

  auto audio_player = emulator->audio_media_player();
  if (!audio_player) {
    return;
  }

  // Convert slider value (0-100) to volume (0.0-1.0)
  float volume = value / 100.0f;
  audio_player->SetVolume(volume);

  // Update label
  volume_value_label_->setText(SafeQString(fmt::format("{}%", value)));
}

}  // namespace app
}  // namespace xe
