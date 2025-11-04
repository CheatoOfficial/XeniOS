/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_GAME_CONFIG_DIALOG_QT_H_
#define XENIA_UI_GAME_CONFIG_DIALOG_QT_H_

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <filesystem>
#include <map>
#include <string>

namespace cvar {
class IConfigVar;
}  // namespace cvar

namespace xe {
namespace app {

class EmulatorWindow;

class GameConfigDialogQt : public QDialog {
  Q_OBJECT

 public:
  GameConfigDialogQt(QWidget* parent, EmulatorWindow* emulator_window,
                     uint32_t title_id, const std::string& game_title);
  ~GameConfigDialogQt() override;

 private slots:
  void OnAddOverrideClicked();
  void OnUseRecommendedClicked();
  void OnSaveClicked();
  void OnCancelClicked();

 private:
  void SetupUI();
  void LoadConfigOverrides();
  void SaveConfigOverrides();
  void LoadRecommendedSettings();
  bool ApplyRecommendedSetting(const std::string& var_name,
                               const std::string& value);
  std::filesystem::path GetGameConfigPath() const;
  QWidget* CreateEditorForCvar(cvar::IConfigVar* var,
                               const std::string& current_value);
  std::string GetEditorValue(QWidget* editor);
  void UpdateRowModifiedState(int row);
  void CreateRow(const std::string& var_name, const std::string& value);

  EmulatorWindow* emulator_window_;
  uint32_t title_id_;
  std::string game_title_;
  std::map<std::string, std::string> config_overrides_;
  std::map<std::string, std::string>
      original_overrides_;  // Track original values
  bool has_unsaved_changes_ = false;

  // UI widgets
  QTableWidget* overrides_table_;
  QPushButton* add_button_;
  QPushButton* recommended_button_;
  QPushButton* save_button_;
  QPushButton* cancel_button_;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_UI_GAME_CONFIG_DIALOG_QT_H_
