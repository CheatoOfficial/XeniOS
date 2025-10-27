/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_CONFIG_DIALOG_QT_H_
#define XENIA_UI_CONFIG_DIALOG_QT_H_

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "xenia/base/cvar.h"

namespace xe {
namespace app {

class EmulatorWindow;

class ConfigDialogQt : public QDialog {
  Q_OBJECT

 public:
  ConfigDialogQt(QWidget* parent, EmulatorWindow* emulator_window);
  ~ConfigDialogQt() override;

  void SelectCategory(const std::string& category_name);

 private slots:
  void OnSaveClicked();
  void OnDiscardClicked();
  void OnResetToDefaultsClicked();
  void OnValueChanged();
  void OnCategorySelected(int index);

 private:
  struct ConfigVarInfo {
    cvar::IConfigVar* var;
    std::string name;
    std::string description;
    std::string category;
    std::string current_value;
    std::string pending_value;
    bool is_modified;
    QWidget* editor_widget =
        nullptr;  // The actual input widget (QLineEdit, QCheckBox, etc.)
  };

  void LoadConfigValues();
  void SaveConfigChanges();
  void ResetToDefaults();
  void SetupUI();
  void CreateCategoryPage(const std::string& category_name,
                          const std::vector<ConfigVarInfo*>& vars);
  QWidget* CreateEditorWidget(ConfigVarInfo* var_info);
  void UpdateEditorFromPendingValue(ConfigVarInfo* var_info);
  void UpdatePendingValueFromEditor(ConfigVarInfo* var_info);
  std::string GetEditorValue(QWidget* editor, const std::string& var_name);

  EmulatorWindow* emulator_window_;
  std::vector<ConfigVarInfo> config_vars_;
  std::map<std::string, std::vector<ConfigVarInfo*>> categories_;
  std::vector<std::string> category_order_;  // To maintain order

  // UI widgets
  QListWidget* category_list_;
  QStackedWidget* settings_stack_;
  QPushButton* save_button_;
  QPushButton* discard_button_;
  QPushButton* reset_button_;

  bool has_unsaved_changes_ = false;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_CONFIG_DIALOG_QT_H_
