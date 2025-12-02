/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_PROFILE_DIALOG_QT_H_
#define XENIA_UI_PROFILE_DIALOG_QT_H_

#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>
#include <map>
#include <memory>

#include "xenia/ui/gamepad_dialog_qt.h"
#include "xenia/xbox.h"

namespace xe {
namespace app {

class EmulatorWindow;

class ProfileDialogQt : public ui::GamepadDialog {
  Q_OBJECT

 public:
  ProfileDialogQt(QWidget* parent, EmulatorWindow* emulator_window,
                  hid::InputSystem* input_system);
  ~ProfileDialogQt() override;

  void RefreshProfiles();

 private slots:
  void OnProfileItemClicked(QListWidgetItem* item);
  void OnProfileItemDoubleClicked(QListWidgetItem* item);
  void OnProfileContextMenu(const QPoint& pos);
  void OnCreateProfileClicked();
  void OnCloseClicked();

 private:
  void SetupUI();
  void LoadProfileIcons();
  QPixmap LoadProfileIcon(uint64_t xuid);
  void PopulateProfileList();

  EmulatorWindow* emulator_window_;
  std::map<uint64_t, QPixmap> profile_icons_;

  // UI widgets
  QListWidget* profile_list_;
  QPushButton* create_profile_button_;
  QPushButton* close_button_;

  uint64_t selected_xuid_ = 0;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_PROFILE_DIALOG_QT_H_
