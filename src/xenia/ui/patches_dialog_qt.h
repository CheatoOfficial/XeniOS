/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_PATCHES_DIALOG_QT_H_
#define XENIA_UI_PATCHES_DIALOG_QT_H_

#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace xe {
namespace app {

class EmulatorWindow;

class PatchesDialogQt : public QDialog {
  Q_OBJECT

 public:
  PatchesDialogQt(QWidget* parent, EmulatorWindow* emulator_window,
                  uint32_t title_id, const std::filesystem::path& patch_file);
  ~PatchesDialogQt() override;

 private:
  struct PatchInfo {
    std::string name;
    std::string description;
    std::string author;
    bool is_enabled;
    size_t patch_index;  // Index in the TOML file
    QCheckBox* checkbox;
  };

  void SetupUI();
  void LoadPatchFile();
  std::filesystem::path GetStorageRootPatchPath();
  void SavePatchToggle(size_t patch_index, bool new_value);
  bool UpdateSinglePatchEnabledLine(std::vector<std::string>& lines,
                                    size_t patch_index, bool new_value);

  EmulatorWindow* emulator_window_;
  uint32_t title_id_;
  std::filesystem::path
      patch_file_;  // Original file location (could be exe_dir or storage_root)
  std::vector<PatchInfo> patches_;

  // UI widgets
  QScrollArea* scroll_area_;
  QWidget* patches_container_;
  QVBoxLayout* patches_layout_;
  QLabel* info_label_;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_UI_PATCHES_DIALOG_QT_H_
