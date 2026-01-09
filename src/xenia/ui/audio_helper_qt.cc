/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/audio_helper_qt.h"

#include <QUrl>
#include <filesystem>

#include "xenia/base/cvar.h"
#include "xenia/base/string.h"

DECLARE_path(achievement_sound_path);

namespace xe {
namespace ui {

AudioHelperQt& AudioHelperQt::Instance() {
  static AudioHelperQt instance;
  return instance;
}

AudioHelperQt::AudioHelperQt() : QObject(nullptr) {}

AudioHelperQt::~AudioHelperQt() {
  if (media_player_) {
    media_player_->stop();
    delete media_player_;
    media_player_ = nullptr;
  }
  if (audio_output_) {
    delete audio_output_;
    audio_output_ = nullptr;
  }
}

void AudioHelperQt::InitializeMediaPlayer() {
  if (initialized_) {
    return;
  }
  initialized_ = true;

  if (cvars::achievement_sound_path.empty()) {
    return;
  }

  std::filesystem::path sound_path = cvars::achievement_sound_path;
  if (!std::filesystem::exists(sound_path)) {
    return;
  }

  // Create without parent to avoid interfering with Qt's widget event loop
  // This prevents FPS drops when the media player is instantiated
  media_player_ = new QMediaPlayer(nullptr);
  audio_output_ = new QAudioOutput(nullptr);
  media_player_->setAudioOutput(audio_output_);
  media_player_->setSource(QUrl::fromLocalFile(
      QString::fromStdString(xe::path_to_utf8(sound_path))));
  audio_output_->setVolume(1.0);
}

void AudioHelperQt::PlayAchievementSound() {
  // Lazy initialization - only create media player when first needed
  if (!initialized_) {
    InitializeMediaPlayer();
  }

  if (media_player_) {
    // Stop any currently playing sound and restart
    media_player_->stop();
    media_player_->setPosition(0);
    media_player_->play();
  }
}

}  // namespace ui
}  // namespace xe
