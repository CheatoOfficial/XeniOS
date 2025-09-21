/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_CONFIG_DIALOG_H_
#define XENIA_APP_CONFIG_DIALOG_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "xenia/base/cvar.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"

namespace xe {
namespace app {

class EmulatorWindow;

class ConfigDialog final : public ui::ImGuiDialog {
 public:
  ConfigDialog(ui::ImGuiDrawer* imgui_drawer, EmulatorWindow* emulator_window);

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void LoadConfigValues();
  void SaveConfigChanges();
  void ResetToDefaults();

  struct ConfigVarInfo {
    cvar::IConfigVar* var;
    std::string name;
    std::string description;
    std::string category;
    std::string current_value;
    std::string pending_value;
    bool is_modified;
    bool is_transient;
  };

  struct CategoryDisplay {
    std::string name;
    std::vector<ConfigVarInfo*> vars;
    bool is_expanded = false;
  };

  EmulatorWindow* emulator_window_;
  std::map<std::string, CategoryDisplay> categories_;
  std::vector<ConfigVarInfo> config_vars_;
  std::string filter_text_;
  bool has_unsaved_changes_ = false;
  bool filter_modified_only_ = false;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_CONFIG_DIALOG_H_
