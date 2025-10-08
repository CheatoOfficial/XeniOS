/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_GAME_LIST_DIALOG_QT_H_
#define XENIA_APP_GAME_LIST_DIALOG_QT_H_

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace xe {
namespace app {

class EmulatorWindow;

struct GameListEntry {
  std::string title_name;
  std::filesystem::path path_to_file;
  time_t last_run_time;
  uint32_t title_id;
  std::vector<uint8_t> icon;
};

class GameListDialogQt : public QWidget {
  Q_OBJECT

 public:
  GameListDialogQt(QWidget* parent, EmulatorWindow* emulator_window);
  ~GameListDialogQt() override;

  void LoadGameList();
  void RefreshIcons();

 private slots:
  void OnFilterTextChanged(const QString& text);
  void OnGameDoubleClicked(int row, int column);
  void OnGameRightClicked(const QPoint& pos);
  void OnPlayClicked();
  void OnSettingsClicked();
  void OnProfileClicked();
  void OnProfileContextMenu(const QPoint& pos);
  void OnSelectionChanged();
  void UpdatePlayButtonState();
  void UpdateProfileButtonState();

 protected:
  bool eventFilter(QObject* obj, QEvent* event) override;

 private:
  void SetupUI();
  void ShowScrollbar();
  void HideScrollbar();
  void PopulateTable();
  void TryLoadIcons();
  QPixmap CreateIconPixmap(const std::vector<uint8_t>& icon_data);
  void LaunchGame(const std::filesystem::path& path);
  void OpenContainingFolder(const std::filesystem::path& path);
  std::string FormatLastPlayed(time_t timestamp);

  EmulatorWindow* emulator_window_;
  std::vector<GameListEntry> game_entries_;
  std::map<uint32_t, QPixmap> title_icons_;

  // UI widgets
  QTableWidget* table_widget_;
  QLineEdit* search_box_;
  QToolButton* play_button_;
  QToolButton* settings_button_;
  QToolButton* profile_button_;
  QLabel* play_icon_label_;
  QLabel* play_label_;
  QLabel* profile_label_;
  QLabel* profile_question_label_;
  QTimer* game_state_timer_;

  int last_logged_in_count_ = 0;
  bool has_logged_in_profile_ = false;
  bool last_game_running_state_ = false;
  std::filesystem::path selected_game_path_;
  uint64_t current_profile_xuid_ = 0;
  QPointer<class ProfileDialogQt> profile_dialog_;
  QTimer* scrollbar_hide_timer_ = nullptr;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_GAME_LIST_DIALOG_QT_H_
