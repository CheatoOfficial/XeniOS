/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/notification_widget_qt.h"

#include <QUrl>
#include <QVBoxLayout>
#include <filesystem>

#include "xenia/base/cvar.h"
#include "xenia/base/string.h"
#include "xenia/ui/qt_util.h"

DEFINE_path(notification_sound_path, "",
            "Path (including filename) to selected notification sound. "
            "Supports WAV, MP3, OGG, FLAC, and other common formats.",
            "UI");

namespace xe {
namespace app {

using xe::ui::SafeQString;

NotificationWidgetQt::NotificationWidgetQt(QWidget* parent,
                                           const QString& title,
                                           const QString& message,
                                           int duration_ms)
    : QWidget(parent), media_player_(nullptr), audio_output_(nullptr) {
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

  // Setup media player for notification sound
  if (!cvars::notification_sound_path.empty()) {
    std::filesystem::path sound_path = cvars::notification_sound_path;
    if (std::filesystem::exists(sound_path)) {
      media_player_ = new QMediaPlayer(this);
      audio_output_ = new QAudioOutput(this);
      media_player_->setAudioOutput(audio_output_);
      media_player_->setSource(
          QUrl::fromLocalFile(SafeQString(xe::path_to_utf8(sound_path))));
      audio_output_->setVolume(1.0);
    }
  }
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

  // Play sound if configured
  if (media_player_) {
    media_player_->play();
  }
}

}  // namespace app
}  // namespace xe
