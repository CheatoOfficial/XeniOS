/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_ACHIEVEMENTS_DIALOG_QT_H_
#define XENIA_UI_ACHIEVEMENTS_DIALOG_QT_H_

#include <QCheckBox>
#include <QLabel>
#include <QPixmap>
#include <QProgressBar>
#include <QScrollArea>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <map>
#include <memory>
#include <vector>

#include "xenia/ui/gamepad_dialog_qt.h"

namespace xe {
namespace kernel {
class KernelState;
namespace xam {
struct Achievement;
struct TitleInfo;
class UserProfile;
}  // namespace xam
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace ui {

class AchievementsDialogQt : public GamepadDialog {
  Q_OBJECT

 public:
  AchievementsDialogQt(QWidget* parent, kernel::KernelState* kernel_state,
                       const kernel::xam::TitleInfo* title_info,
                       const kernel::xam::UserProfile* profile);
  ~AchievementsDialogQt() override;

 private slots:
  void OnShowLockedInfoChanged();
  void OnRefreshClicked();

 private:
  void SetupUI();
  void LoadAchievements();
  void PopulateAchievements();
  QPixmap GetAchievementIcon(const xe::kernel::xam::Achievement& achievement);
  QString GetAchievementTitle(const xe::kernel::xam::Achievement& achievement);
  QString GetAchievementDescription(
      const xe::kernel::xam::Achievement& achievement);
  QString GetUnlockedTime(const xe::kernel::xam::Achievement& achievement);
  void UpdateSummary();

  kernel::KernelState* kernel_state_;
  const kernel::xam::TitleInfo* title_info_;
  const kernel::xam::UserProfile* profile_;
  uint64_t xuid_;
  uint32_t title_id_;
  QString title_name_;

  // UI elements
  QTableWidget* achievements_table_;
  QCheckBox* show_locked_checkbox_;
  QLabel* summary_label_;
  QProgressBar* progress_bar_;

  // Achievement data
  std::vector<xe::kernel::xam::Achievement> achievements_;
  std::map<uint32_t, QPixmap> achievement_icons_;
  QPixmap lock_icon_;
  bool show_locked_info_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_ACHIEVEMENTS_DIALOG_QT_H_
