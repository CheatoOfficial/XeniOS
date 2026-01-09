/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_AUDIO_HELPER_QT_H_
#define XENIA_UI_AUDIO_HELPER_QT_H_

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QObject>

namespace xe {
namespace ui {

// Singleton audio helper for playing notification sounds.
// Uses QMediaPlayer which doesn't require a QWidget parent,
// so it works with both QWidget and QWindow-based windows.
class AudioHelperQt : public QObject {
  Q_OBJECT

 public:
  static AudioHelperQt& Instance();

  // Play the achievement unlock sound if configured
  void PlayAchievementSound();

 private:
  AudioHelperQt();
  ~AudioHelperQt();

  // Non-copyable
  AudioHelperQt(const AudioHelperQt&) = delete;
  AudioHelperQt& operator=(const AudioHelperQt&) = delete;

  void InitializeMediaPlayer();

  QMediaPlayer* media_player_ = nullptr;
  QAudioOutput* audio_output_ = nullptr;
  bool initialized_ = false;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_AUDIO_HELPER_QT_H_
