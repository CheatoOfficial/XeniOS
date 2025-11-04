/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_SIMPLE_CONFIG_DIALOG_QT_H_
#define XENIA_UI_SIMPLE_CONFIG_DIALOG_QT_H_

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>
#include <map>
#include <string>

#include "xenia/base/cvar.h"

namespace xe {
namespace app {

class EmulatorWindow;

class SimpleConfigDialogQt : public QDialog {
  Q_OBJECT

 public:
  SimpleConfigDialogQt(QWidget* parent, EmulatorWindow* emulator_window);
  ~SimpleConfigDialogQt() override;

  void ReloadConfigValues();

 private slots:
  void OnSaveClicked();
  void OnDiscardClicked();
  void OnAdvancedClicked();
  void OnResetClicked();
  void OnValueChanged();

 private:
  struct ConfigOption {
    std::string cvar_name;
    std::string current_value;
    std::string pending_value;
    QWidget* editor_widget = nullptr;
    QLabel* label_widget = nullptr;
  };

  void LoadConfigValues();
  void SaveConfigChanges();
  void SetupUI();
  void UpdatePendingValueFromEditor(ConfigOption* option);
  void UpdateLabelModifiedState(ConfigOption* option);
  std::string GetEditorValue(QWidget* editor, const std::string& cvar_name);

  EmulatorWindow* emulator_window_;
  std::map<std::string, ConfigOption> options_;

  bool has_unsaved_changes_ = false;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_UI_SIMPLE_CONFIG_DIALOG_QT_H_
