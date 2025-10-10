/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_PROFILE_EDITOR_DIALOG_QT_H_
#define XENIA_APP_PROFILE_EDITOR_DIALOG_QT_H_

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTextEdit>
#include <map>
#include <memory>
#include <unordered_map>

#include "xenia/kernel/xam/user_settings.h"
#include "xenia/kernel/xam/xam.h"
#include "xenia/xbox.h"

// Hash specialization for UserSettingId to work with unordered_map
namespace std {
template <>
struct hash<xe::kernel::xam::UserSettingId> {
  size_t operator()(const xe::kernel::xam::UserSettingId& k) const {
    return hash<uint32_t>()(static_cast<uint32_t>(k));
  }
};
}  // namespace std

namespace xe {
namespace app {

class EmulatorWindow;

class ProfileEditorDialogQt : public QDialog {
  Q_OBJECT

 public:
  ProfileEditorDialogQt(QWidget* parent, EmulatorWindow* emulator_window,
                        uint64_t xuid);
  ~ProfileEditorDialogQt() override;

 private slots:
  void OnChangeIconClicked();
  void OnSaveClicked();
  void OnCancelClicked();

 private:
  void SetupUI();
  void LoadProfileData();
  void SaveProfileData();
  void LoadProfileIcon();
  bool ValidateGamertagInput(const QString& text);
  void UpdateGamertagValidation();

  EmulatorWindow* emulator_window_;
  uint64_t xuid_;

  // Profile data
  struct ProfileData {
    // Account settings
    std::string gamertag;
    xe::XOnlineCountry country;
    xe::XLanguage language;
    bool is_live_enabled;
    std::string online_xuid;
    std::string online_domain;
    kernel::xam::X_XAMACCOUNTINFO::AccountSubscriptionTier
        account_subscription_tier;

    // GPD settings
    std::map<kernel::xam::UserSettingId, kernel::xam::UserDataTypes>
        gpd_settings;

    // GPD string buffers
    std::string gamer_name;
    std::string gamer_motto;
    std::string gamer_bio;

    // Profile icon
    std::vector<uint8_t> profile_icon;
  };

  ProfileData original_data_;
  ProfileData current_data_;

  // UI widgets - Base Settings
  QLabel* icon_label_;
  QPushButton* change_icon_button_;
  QLineEdit* gamertag_edit_;
  QLabel* gamertag_validation_label_;
  QLineEdit* gamer_name_edit_;
  QLineEdit* gamer_motto_edit_;
  QTextEdit* gamer_bio_edit_;
  QComboBox* language_combo_;
  QComboBox* country_combo_;

  // UI widgets - Online Settings
  QCheckBox* live_enabled_checkbox_;
  QLineEdit* online_xuid_edit_;
  QLineEdit* online_domain_edit_;
  QComboBox* gamer_zone_combo_;
  QComboBox* subscription_tier_combo_;

  // UI widgets - Game Settings
  QComboBox* difficulty_combo_;
  QComboBox* controller_vibration_combo_;
  QComboBox* control_sensitivity_combo_;
  QComboBox* preferred_color_first_combo_;
  QComboBox* preferred_color_second_combo_;

  // UI widgets - Action Game Settings
  QComboBox* yaxis_inversion_combo_;
  QComboBox* auto_aim_combo_;
  QComboBox* auto_center_combo_;
  QComboBox* movement_control_combo_;

  // UI widgets - Racing Game Settings
  QComboBox* transmission_combo_;
  QComboBox* camera_location_combo_;
  QComboBox* brake_control_combo_;
  QComboBox* accelerator_control_combo_;

  // Buttons
  QPushButton* save_button_;
  QPushButton* cancel_button_;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_PROFILE_EDITOR_DIALOG_QT_H_
