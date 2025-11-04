/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/simple_config_dialog_qt.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "xenia/app/emulator_window.h"
#include "xenia/base/cvar.h"
#include "xenia/config.h"
#include "xenia/ui/config_dialog_qt.h"
#include "xenia/ui/config_helpers.h"
#include "xenia/ui/qt_util.h"

namespace xe {
namespace app {

using xe::ui::SafeQString;
using xe::ui::SafeStdString;

SimpleConfigDialogQt::SimpleConfigDialogQt(QWidget* parent,
                                           EmulatorWindow* emulator_window)
    : QDialog(parent), emulator_window_(emulator_window) {
  setWindowTitle("Emulator Settings");
  setMinimumWidth(500);
  SetupUI();
  LoadConfigValues();
}

SimpleConfigDialogQt::~SimpleConfigDialogQt() = default;

void SimpleConfigDialogQt::ReloadConfigValues() { LoadConfigValues(); }

void SimpleConfigDialogQt::SetupUI() {
  auto* main_layout = new QVBoxLayout(this);

  // Audio section
  auto* audio_group = new QGroupBox("Audio", this);
  auto* audio_layout = new QFormLayout(audio_group);

  auto* apu_combo = new QComboBox(this);
  const auto& enum_options = ui::GetKnownEnumOptions();
  auto apu_it = enum_options.find("apu");
  if (apu_it != enum_options.end()) {
    for (const auto& opt : apu_it->second) {
      apu_combo->addItem(SafeQString(opt));
    }
  }
  options_["apu"].cvar_name = "apu";
  options_["apu"].editor_widget = apu_combo;
  options_["apu"].label_widget = new QLabel("Audio Backend:", this);
  connect(apu_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &SimpleConfigDialogQt::OnValueChanged);
  audio_layout->addRow(options_["apu"].label_widget, apu_combo);

  auto* xma_combo = new QComboBox(this);
  auto xma_it = enum_options.find("xma_decoder");
  if (xma_it != enum_options.end()) {
    for (const auto& opt : xma_it->second) {
      xma_combo->addItem(SafeQString(opt));
    }
  }
  options_["xma_decoder"].cvar_name = "xma_decoder";
  options_["xma_decoder"].editor_widget = xma_combo;
  options_["xma_decoder"].label_widget = new QLabel("Audio Decoder:", this);
  connect(xma_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &SimpleConfigDialogQt::OnValueChanged);
  audio_layout->addRow(options_["xma_decoder"].label_widget, xma_combo);

  auto* xma_thread_check = new QCheckBox(this);
  options_["use_dedicated_xma_thread"].cvar_name = "use_dedicated_xma_thread";
  options_["use_dedicated_xma_thread"].editor_widget = xma_thread_check;
  options_["use_dedicated_xma_thread"].label_widget =
      new QLabel("Dedicated Thread:", this);
  connect(xma_thread_check, &QCheckBox::checkStateChanged, this,
          &SimpleConfigDialogQt::OnValueChanged);
  audio_layout->addRow(options_["use_dedicated_xma_thread"].label_widget,
                       xma_thread_check);

  main_layout->addWidget(audio_group);

  // Graphics section
  auto* graphics_group = new QGroupBox("Graphics", this);
  auto* graphics_layout = new QFormLayout(graphics_group);

  auto* gpu_combo = new QComboBox(this);
  auto gpu_it = enum_options.find("gpu");
  if (gpu_it != enum_options.end()) {
    for (const auto& opt : gpu_it->second) {
      gpu_combo->addItem(SafeQString(opt));
    }
  }
  options_["gpu"].cvar_name = "gpu";
  options_["gpu"].editor_widget = gpu_combo;
  options_["gpu"].label_widget = new QLabel("Graphics Backend:", this);
  connect(gpu_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &SimpleConfigDialogQt::OnValueChanged);
  graphics_layout->addRow(options_["gpu"].label_widget, gpu_combo);

  auto* render_path_combo = new QComboBox(this);
  auto render_path_it = enum_options.find("render_target_path");
  if (render_path_it != enum_options.end()) {
    for (const auto& opt : render_path_it->second) {
      render_path_combo->addItem(SafeQString(opt));
    }
  }
  options_["render_target_path"].cvar_name = "render_target_path";
  options_["render_target_path"].editor_widget = render_path_combo;
  options_["render_target_path"].label_widget = new QLabel("Rendering:", this);
  connect(render_path_combo,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &SimpleConfigDialogQt::OnValueChanged);
  graphics_layout->addRow(options_["render_target_path"].label_widget,
                          render_path_combo);

  auto* scale_combo = new QComboBox(this);
  scale_combo->addItem("Native", 1);
  scale_combo->addItem("2x", 2);
  scale_combo->addItem("3x", 3);
  scale_combo->addItem("4x", 4);
  scale_combo->addItem("5x", 5);
  scale_combo->addItem("6x", 6);
  scale_combo->addItem("7x", 7);
  scale_combo->addItem("8x", 8);
  options_["draw_resolution_scale"].cvar_name = "draw_resolution_scale";
  options_["draw_resolution_scale"].editor_widget = scale_combo;
  options_["draw_resolution_scale"].label_widget =
      new QLabel("Resolution Scale:", this);
  connect(scale_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SimpleConfigDialogQt::OnValueChanged);
  graphics_layout->addRow(options_["draw_resolution_scale"].label_widget,
                          scale_combo);

  connect(gpu_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          [scale_combo, gpu_combo]() {
            int current_value = scale_combo->currentData().toInt();
            if (SafeStdString(gpu_combo->currentText()) == "vulkan") {
              while (scale_combo->count() > 3) {
                scale_combo->removeItem(scale_combo->count() - 1);
              }
            } else {
              if (scale_combo->count() < 8) {
                scale_combo->addItem("4x", 4);
                scale_combo->addItem("5x", 5);
                scale_combo->addItem("6x", 6);
                scale_combo->addItem("7x", 7);
                scale_combo->addItem("8x", 8);
              }
            }
            int index = scale_combo->findData(current_value);
            if (index >= 0) {
              scale_combo->setCurrentIndex(index);
            } else {
              scale_combo->setCurrentIndex(0);
            }
          });

  auto* fps_spin = new QSpinBox(this);
  fps_spin->setMinimum(0);
  fps_spin->setMaximum(1000);
  fps_spin->setSpecialValueText("Unlimited");
  options_["framerate_limit"].cvar_name = "framerate_limit";
  options_["framerate_limit"].editor_widget = fps_spin;
  options_["framerate_limit"].label_widget = new QLabel("FPS Limit:", this);
  connect(fps_spin, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &SimpleConfigDialogQt::OnValueChanged);
  graphics_layout->addRow(options_["framerate_limit"].label_widget, fps_spin);

  auto* vsync_check = new QCheckBox(this);
  options_["vsync"].cvar_name = "vsync";
  options_["vsync"].editor_widget = vsync_check;
  options_["vsync"].label_widget = new QLabel("VSync:", this);
  connect(vsync_check, &QCheckBox::checkStateChanged, this,
          &SimpleConfigDialogQt::OnValueChanged);
  graphics_layout->addRow(options_["vsync"].label_widget, vsync_check);

  auto* fullscreen_check = new QCheckBox(this);
  options_["fullscreen"].cvar_name = "fullscreen";
  options_["fullscreen"].editor_widget = fullscreen_check;
  options_["fullscreen"].label_widget = new QLabel("Full Screen:", this);
  connect(fullscreen_check, &QCheckBox::checkStateChanged, this,
          &SimpleConfigDialogQt::OnValueChanged);
  graphics_layout->addRow(options_["fullscreen"].label_widget,
                          fullscreen_check);

  auto* letterbox_check = new QCheckBox(this);
  options_["present_letterbox"].cvar_name = "present_letterbox";
  options_["present_letterbox"].editor_widget = letterbox_check;
  options_["present_letterbox"].label_widget = new QLabel("Letterbox:", this);
  connect(letterbox_check, &QCheckBox::checkStateChanged, this,
          &SimpleConfigDialogQt::OnValueChanged);
  graphics_layout->addRow(options_["present_letterbox"].label_widget,
                          letterbox_check);

  auto* vrr_check = new QCheckBox(this);
  options_["variable_refresh_rate"].cvar_name = "variable_refresh_rate";
  options_["variable_refresh_rate"].editor_widget = vrr_check;
  options_["variable_refresh_rate"].label_widget =
      new QLabel("Variable Refresh Rate:", this);
  connect(vrr_check, &QCheckBox::checkStateChanged, this,
          &SimpleConfigDialogQt::OnValueChanged);
  graphics_layout->addRow(options_["variable_refresh_rate"].label_widget,
                          vrr_check);

  main_layout->addWidget(graphics_group);

  // Other section
  auto* other_group = new QGroupBox("Other", this);
  auto* other_layout = new QFormLayout(other_group);

  auto* license_check = new QCheckBox(this);
  options_["license_mask"].cvar_name = "license_mask";
  options_["license_mask"].editor_widget = license_check;
  options_["license_mask"].label_widget = new QLabel("Enable License:", this);
  connect(license_check, &QCheckBox::checkStateChanged, this,
          &SimpleConfigDialogQt::OnValueChanged);
  other_layout->addRow(options_["license_mask"].label_widget, license_check);

  auto* discord_check = new QCheckBox(this);
  options_["discord"].cvar_name = "discord";
  options_["discord"].editor_widget = discord_check;
  options_["discord"].label_widget = new QLabel("Discord Rich Presence:", this);
  connect(discord_check, &QCheckBox::checkStateChanged, this,
          &SimpleConfigDialogQt::OnValueChanged);
  other_layout->addRow(options_["discord"].label_widget, discord_check);

  auto* language_combo = new QComboBox(this);
  auto language_it = enum_options.find("user_language");
  if (language_it != enum_options.end()) {
    for (const auto& opt : language_it->second) {
      language_combo->addItem(SafeQString(opt));
    }
  }
  options_["user_language"].cvar_name = "user_language";
  options_["user_language"].editor_widget = language_combo;
  options_["user_language"].label_widget = new QLabel("Language:", this);
  connect(language_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SimpleConfigDialogQt::OnValueChanged);
  other_layout->addRow(options_["user_language"].label_widget, language_combo);

  auto* country_combo = new QComboBox(this);
  auto country_it = enum_options.find("user_country");
  if (country_it != enum_options.end()) {
    for (const auto& opt : country_it->second) {
      country_combo->addItem(SafeQString(opt));
    }
  }
  options_["user_country"].cvar_name = "user_country";
  options_["user_country"].editor_widget = country_combo;
  options_["user_country"].label_widget = new QLabel("Country:", this);
  connect(country_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SimpleConfigDialogQt::OnValueChanged);
  other_layout->addRow(options_["user_country"].label_widget, country_combo);

  main_layout->addWidget(other_group);

  // Buttons
  auto* button_layout = new QHBoxLayout();

  auto* advanced_button = new QPushButton("Advanced...", this);
  connect(advanced_button, &QPushButton::clicked, this,
          &SimpleConfigDialogQt::OnAdvancedClicked);
  button_layout->addWidget(advanced_button);

  auto* reset_button = new QPushButton("Reset to Default", this);
  connect(reset_button, &QPushButton::clicked, this,
          &SimpleConfigDialogQt::OnResetClicked);
  button_layout->addWidget(reset_button);

  button_layout->addStretch();

  auto* save_button = new QPushButton("Save", this);
  connect(save_button, &QPushButton::clicked, this,
          &SimpleConfigDialogQt::OnSaveClicked);
  button_layout->addWidget(save_button);

  auto* discard_button = new QPushButton("Cancel", this);
  connect(discard_button, &QPushButton::clicked, this,
          &SimpleConfigDialogQt::OnDiscardClicked);
  button_layout->addWidget(discard_button);

  main_layout->addLayout(button_layout);
}

void SimpleConfigDialogQt::LoadConfigValues() {
  if (!cvar::ConfigVars) {
    return;
  }

  for (auto& [cvar_name, option] : options_) {
    if (cvar_name == "draw_resolution_scale") {
      auto it_x = (*cvar::ConfigVars).find("draw_resolution_scale_x");
      if (it_x != (*cvar::ConfigVars).end()) {
        auto config_var_x = static_cast<cvar::IConfigVar*>(it_x->second);
        std::string value = config_var_x->config_value();

        if (value.length() >= 2 && value.front() == '"' &&
            value.back() == '"') {
          value = value.substr(1, value.length() - 2);
        }

        option.current_value = value;
        option.pending_value = value;

        if (auto* combo = qobject_cast<QComboBox*>(option.editor_widget)) {
          int scale_value = std::stoi(value);
          int index = combo->findData(scale_value);
          if (index >= 0) {
            combo->setCurrentIndex(index);
          }
        }
      }
      continue;
    }

    if (cvar_name == "variable_refresh_rate") {
      auto it_gpu = (*cvar::ConfigVars).find("gpu");
      std::string gpu_value = "d3d12";
      if (it_gpu != (*cvar::ConfigVars).end()) {
        auto gpu_var = static_cast<cvar::IConfigVar*>(it_gpu->second);
        gpu_value = gpu_var->config_value();
        if (gpu_value.length() >= 2 && gpu_value.front() == '"' &&
            gpu_value.back() == '"') {
          gpu_value = gpu_value.substr(1, gpu_value.length() - 2);
        }
      }

      std::string actual_cvar_name =
          (gpu_value == "vulkan")
              ? "vulkan_allow_present_mode_immediate"
              : "d3d12_allow_variable_refresh_rate_and_tearing";

      auto it_vrr = (*cvar::ConfigVars).find(actual_cvar_name);
      if (it_vrr != (*cvar::ConfigVars).end()) {
        auto config_var_vrr = static_cast<cvar::IConfigVar*>(it_vrr->second);
        std::string value = config_var_vrr->config_value();

        if (value.length() >= 2 && value.front() == '"' &&
            value.back() == '"') {
          value = value.substr(1, value.length() - 2);
        }

        option.current_value = value;
        option.pending_value = value;

        if (auto* check = qobject_cast<QCheckBox*>(option.editor_widget)) {
          check->setChecked(value == "true");
        }
      }
      continue;
    }

    auto it = (*cvar::ConfigVars).find(cvar_name);
    if (it != (*cvar::ConfigVars).end()) {
      auto config_var = static_cast<cvar::IConfigVar*>(it->second);
      std::string value = config_var->config_value();

      if (value.length() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.length() - 2);
      }

      option.current_value = value;
      option.pending_value = value;

      if (auto* combo = qobject_cast<QComboBox*>(option.editor_widget)) {
        int index = combo->findText(SafeQString(value));
        if (index >= 0) {
          combo->setCurrentIndex(index);
        }
      } else if (auto* check = qobject_cast<QCheckBox*>(option.editor_widget)) {
        if (cvar_name == "license_mask") {
          check->setChecked(value == "1");
        } else {
          check->setChecked(value == "true");
        }
      } else if (auto* spin = qobject_cast<QSpinBox*>(option.editor_widget)) {
        spin->setValue(std::stoi(value));
      }
    }
  }

  has_unsaved_changes_ = false;
}

void SimpleConfigDialogQt::SaveConfigChanges() {
  if (!cvar::ConfigVars) {
    return;
  }

  for (auto& [cvar_name, option] : options_) {
    UpdatePendingValueFromEditor(&option);

    if (cvar_name == "draw_resolution_scale") {
      auto it_x = (*cvar::ConfigVars).find("draw_resolution_scale_x");
      auto it_y = (*cvar::ConfigVars).find("draw_resolution_scale_y");
      if (it_x != (*cvar::ConfigVars).end() &&
          it_y != (*cvar::ConfigVars).end()) {
        auto config_var_x = static_cast<cvar::IConfigVar*>(it_x->second);
        auto config_var_y = static_cast<cvar::IConfigVar*>(it_y->second);
        toml::value new_value(std::stoi(option.pending_value));
        config_var_x->LoadConfigValue(&new_value);
        config_var_y->LoadConfigValue(&new_value);
      }
      continue;
    }

    if (cvar_name == "variable_refresh_rate") {
      auto it_gpu = (*cvar::ConfigVars).find("gpu");
      std::string gpu_value = "d3d12";
      if (it_gpu != (*cvar::ConfigVars).end()) {
        auto gpu_var = static_cast<cvar::IConfigVar*>(it_gpu->second);
        gpu_value = gpu_var->config_value();
        if (gpu_value.length() >= 2 && gpu_value.front() == '"' &&
            gpu_value.back() == '"') {
          gpu_value = gpu_value.substr(1, gpu_value.length() - 2);
        }
      }

      std::string actual_cvar_name =
          (gpu_value == "vulkan")
              ? "vulkan_allow_present_mode_immediate"
              : "d3d12_allow_variable_refresh_rate_and_tearing";

      auto it_vrr = (*cvar::ConfigVars).find(actual_cvar_name);
      if (it_vrr != (*cvar::ConfigVars).end()) {
        auto config_var_vrr = static_cast<cvar::IConfigVar*>(it_vrr->second);
        toml::value new_value(option.pending_value == "true");
        config_var_vrr->LoadConfigValue(&new_value);
      }
      continue;
    }

    auto it = (*cvar::ConfigVars).find(cvar_name);
    if (it != (*cvar::ConfigVars).end()) {
      auto config_var = static_cast<cvar::IConfigVar*>(it->second);

      if (cvar_name == "framerate_limit") {
        toml::value new_value(std::stoull(option.pending_value));
        config_var->LoadConfigValue(&new_value);
      } else if (cvar_name == "license_mask") {
        toml::value new_value(std::stoi(option.pending_value));
        config_var->LoadConfigValue(&new_value);
      } else if (cvar_name == "vsync" || cvar_name == "fullscreen" ||
                 cvar_name == "present_letterbox" || cvar_name == "discord" ||
                 cvar_name == "use_dedicated_xma_thread") {
        toml::value new_value(option.pending_value == "true");
        config_var->LoadConfigValue(&new_value);
      } else {
        toml::value new_value(option.pending_value);
        config_var->LoadConfigValue(&new_value);
      }
    }
  }

  config::SaveConfig();
  has_unsaved_changes_ = false;
}

void SimpleConfigDialogQt::OnSaveClicked() {
  SaveConfigChanges();
  accept();
}

void SimpleConfigDialogQt::OnDiscardClicked() {
  if (has_unsaved_changes_) {
    auto reply = QMessageBox::question(
        this, "Unsaved Changes",
        "You have unsaved changes. Are you sure you want to discard them?",
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::No) {
      return;
    }
  }

  reject();
}

void SimpleConfigDialogQt::OnAdvancedClicked() {
  if (has_unsaved_changes_) {
    auto reply = QMessageBox::question(
        this, "Unsaved Changes",
        "You have unsaved changes. Do you want to save them before opening "
        "the advanced settings?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (reply == QMessageBox::Cancel) {
      return;
    } else if (reply == QMessageBox::Save) {
      SaveConfigChanges();
    }
  }

  accept();

  config::ReloadConfig();

  auto* advanced_dialog = new ConfigDialogQt(nullptr, emulator_window_);
  advanced_dialog->exec();
  delete advanced_dialog;
}

void SimpleConfigDialogQt::OnResetClicked() {
  auto reply = QMessageBox::question(
      this, "Reset to Defaults",
      "Are you sure you want to reset all settings to their default values?",
      QMessageBox::Yes | QMessageBox::No);

  if (reply == QMessageBox::Yes) {
    if (!cvar::ConfigVars) {
      return;
    }

    for (auto& [name, var] : *cvar::ConfigVars) {
      if (name.find("logged_profile_slot_") != std::string::npos) {
        continue;
      }
      auto config_var = static_cast<cvar::IConfigVar*>(var);
      config_var->ResetConfigValueToDefault();
    }

    LoadConfigValues();
    has_unsaved_changes_ = true;
  }
}

void SimpleConfigDialogQt::OnValueChanged() {
  for (auto& [cvar_name, option] : options_) {
    UpdatePendingValueFromEditor(&option);
    bool is_modified = (option.pending_value != option.current_value);

    if (option.label_widget) {
      QFont font = option.label_widget->font();
      font.setBold(is_modified);
      option.label_widget->setFont(font);
    }
  }

  has_unsaved_changes_ = true;
}

void SimpleConfigDialogQt::UpdatePendingValueFromEditor(ConfigOption* option) {
  option->pending_value =
      GetEditorValue(option->editor_widget, option->cvar_name);
}

void SimpleConfigDialogQt::UpdateLabelModifiedState(ConfigOption* option) {
  if (!option || !option->label_widget) {
    return;
  }

  bool is_modified = (option->pending_value != option->current_value);
  QFont font = option->label_widget->font();
  font.setBold(is_modified);
  option->label_widget->setFont(font);
}

std::string SimpleConfigDialogQt::GetEditorValue(QWidget* editor,
                                                 const std::string& cvar_name) {
  if (auto* combo = qobject_cast<QComboBox*>(editor)) {
    if (cvar_name == "draw_resolution_scale") {
      return std::to_string(combo->currentData().toInt());
    }
    return SafeStdString(combo->currentText());
  } else if (auto* check = qobject_cast<QCheckBox*>(editor)) {
    if (cvar_name == "license_mask") {
      return check->isChecked() ? "1" : "0";
    } else {
      return check->isChecked() ? "true" : "false";
    }
  } else if (auto* spin = qobject_cast<QSpinBox*>(editor)) {
    return std::to_string(spin->value());
  }
  return "";
}

}  // namespace app
}  // namespace xe
