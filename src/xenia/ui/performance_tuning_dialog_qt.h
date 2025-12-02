/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_PERFORMANCE_TUNING_DIALOG_QT_H_
#define XENIA_UI_PERFORMANCE_TUNING_DIALOG_QT_H_

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialog>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace xe {
namespace app {

class EmulatorWindow;

class PerformanceTuningDialogQt : public QDialog {
  Q_OBJECT

 public:
  PerformanceTuningDialogQt(QWidget* parent, EmulatorWindow* emulator_window);
  ~PerformanceTuningDialogQt() override;

 private slots:
  void OnVsyncChanged(int state);
  void OnOcclusionQueryChanged(int state);
  void OnReadbackResolveChanged(int value);
  void OnReadbackMemexportChanged(int value);
  void OnClearMemoryPageStateChanged(int state);

 protected:
  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  void SetupUI();
  void LoadCurrentSettings();
  void SaveToGameConfig(const char* cvar_name, const std::string& value);
  void SaveToGameConfig(const char* cvar_name, bool value);
  void ShowNotification(const QString& title, const QString& description);

  EmulatorWindow* emulator_window_;
  QPoint drag_position_;
  bool dragging_ = false;

  // Readback Resolve widgets
  QGroupBox* readback_resolve_group_;
  QButtonGroup* readback_resolve_button_group_;

  // Readback Memexport widgets
  QGroupBox* readback_memexport_group_;
  QButtonGroup* readback_memexport_button_group_;

  // Other settings widgets
  QGroupBox* other_group_;
  QCheckBox* vsync_checkbox_;
  QCheckBox* occlusion_query_checkbox_;
  QCheckBox* clear_memory_checkbox_;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_UI_PERFORMANCE_TUNING_DIALOG_QT_H_
