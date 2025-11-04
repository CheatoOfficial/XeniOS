/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/profile_editor_dialog_qt.h"

#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QScrollArea>
#include <QTextCursor>
#include <QVBoxLayout>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/app/emulator_window.h"
#include "xenia/base/logging.h"
#include "xenia/base/png_utils.h"
#include "xenia/base/string_util.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/title_id_utils.h"
#include "xenia/kernel/xam/profile_manager.h"
#include "xenia/kernel/xam/user_settings.h"
#include "xenia/kernel/xam/user_tracker.h"
#include "xenia/kernel/xam/xam_state.h"
#include "xenia/ui/qt_util.h"

namespace xe {
namespace app {

using namespace kernel::xam;
using xe::ui::SafeQString;
using xe::ui::SafeStdString;

// Language names
static const char* kLanguageNames[] = {nullptr,
                                       "English",
                                       "Japanese",
                                       "German",
                                       "French",
                                       "Spanish",
                                       "Italian",
                                       "Korean",
                                       "Traditional Chinese",
                                       "Portuguese",
                                       "Simplified Chinese",
                                       "Polish",
                                       "Russian"};

// Country names
static const char* kCountryNames[] = {nullptr,
                                      "United Arab Emirates",
                                      "Albania",
                                      "Armenia",
                                      "Argentina",
                                      "Austria",
                                      "Australia",
                                      "Azerbaijan",
                                      "Belgium",
                                      "Bulgaria",
                                      "Bahrain",
                                      "Brunei Darussalam",
                                      "Bolivia",
                                      "Brazil",
                                      "Belarus",
                                      "Belize",
                                      "Canada",
                                      nullptr,
                                      "Switzerland",
                                      "Chile",
                                      "China",
                                      "Colombia",
                                      "Costa Rica",
                                      "Czech Republic",
                                      "Germany",
                                      "Denmark",
                                      "Dominican Republic",
                                      "Algeria",
                                      "Ecuador",
                                      "Estonia",
                                      "Egypt",
                                      "Spain",
                                      "Finland",
                                      "Faroe Islands",
                                      "France",
                                      "Great Britain",
                                      "Georgia",
                                      "Greece",
                                      "Guatemala",
                                      "Hong Kong",
                                      "Honduras",
                                      "Croatia",
                                      "Hungary",
                                      "Indonesia",
                                      "Ireland",
                                      "Israel",
                                      "India",
                                      "Iraq",
                                      "Iran",
                                      "Iceland",
                                      "Italy",
                                      "Jamaica",
                                      "Jordan",
                                      "Japan",
                                      "Kenya",
                                      "Kyrgyzstan",
                                      "Korea",
                                      "Kuwait",
                                      "Kazakhstan",
                                      "Lebanon",
                                      "Liechtenstein",
                                      "Lithuania",
                                      "Luxembourg",
                                      "Latvia",
                                      "Libya",
                                      "Morocco",
                                      "Monaco",
                                      "Macedonia",
                                      "Mongolia",
                                      "Macau",
                                      "Maldives",
                                      "Mexico",
                                      "Malaysia",
                                      "Nicaragua",
                                      "Netherlands",
                                      "Norway",
                                      "New Zealand",
                                      "Oman",
                                      "Panama",
                                      "Peru",
                                      "Philippines",
                                      "Pakistan",
                                      "Poland",
                                      "Puerto Rico",
                                      "Portugal",
                                      "Paraguay",
                                      "Qatar",
                                      "Romania",
                                      "Russian Federation",
                                      "Saudi Arabia",
                                      "Sweden",
                                      "Singapore",
                                      "Slovenia",
                                      "Slovak Republic",
                                      nullptr,
                                      "El Salvador",
                                      "Syria",
                                      "Thailand",
                                      "Tunisia",
                                      "Turkey",
                                      "Trinidad And Tobago",
                                      "Taiwan",
                                      "Ukraine",
                                      "United States",
                                      "Uruguay",
                                      "Uzbekistan",
                                      "Venezuela",
                                      "Viet Nam",
                                      "Yemen",
                                      "South Africa",
                                      "Zimbabwe"};

// Subscription tier names
static const char* kSubscriptionTierNames[] = {
    "None",  nullptr, nullptr, "Silver", nullptr,
    nullptr, "Gold",  nullptr, nullptr,  "Family"};

// Gamer zone names
static const char* kGamerZoneNames[] = {"None", "Recreation", "Pro", "Family",
                                        "Underground"};

// Preferred color names
static const char* kPreferredColorNames[] = {
    "None", "Black",  "White", "Yellow", "Orange", "Pink",
    "Red",  "Purple", "Blue",  "Green",  "Brown",  "Silver"};

// Controller vibration options
static const char* kControllerVibrationOptions[] = {"Off", nullptr, nullptr,
                                                    "On"};

// Control sensitivity options
static const char* kControlSensitivityOptions[] = {"Medium", "Low", "High"};

// Gamer difficulty options
static const char* kGamerDifficultyOptions[] = {"Normal", "Easy", "Hard"};

// Auto aim options
static const char* kAutoAimOptions[] = {"Off", "On"};

// Auto center options
static const char* kAutoCenterOptions[] = {"Off", "On"};

// Movement control options
static const char* kMovementControlOptions[] = {"Left Thumbstick",
                                                "Right Thumbstick"};

// Y-axis inversion options
static const char* kYAxisInversionOptions[] = {"Off", "On"};

// Transmission options
static const char* kTransmissionOptions[] = {"Automatic", "Manual"};

// Camera location options
static const char* kCameraLocationOptions[] = {"Behind", "In Front", "Inside"};

// Brake control options
static const char* kBrakeControlOptions[] = {"Trigger", "Button"};

// Accelerator control options
static const char* kAcceleratorControlOptions[] = {"Trigger", "Button"};

ProfileEditorDialogQt::ProfileEditorDialogQt(QWidget* parent,
                                             EmulatorWindow* emulator_window,
                                             uint64_t xuid)
    : QDialog(parent),
      emulator_window_(emulator_window),
      xuid_(xuid),
      original_data_{},
      current_data_{} {
  LoadProfileData();
  SetupUI();
}

ProfileEditorDialogQt::~ProfileEditorDialogQt() {
  // Cleanup handled by Qt
}

void ProfileEditorDialogQt::LoadProfileData() {
  if (!emulator_window_ || !emulator_window_->emulator()) {
    return;
  }

  auto kernel_state = emulator_window_->emulator()->kernel_state();
  if (!kernel_state) {
    return;
  }

  auto xam_state = kernel_state->xam_state();
  if (!xam_state) {
    return;
  }

  auto profile_manager = xam_state->profile_manager();
  if (!profile_manager) {
    return;
  }

  const auto account_data = profile_manager->GetAccount(xuid_);
  if (!account_data) {
    return;
  }

  // Load account data
  original_data_.gamertag = account_data->GetGamertagString();
  original_data_.country = account_data->GetCountry();
  original_data_.language = account_data->GetLanguage();
  original_data_.is_live_enabled = account_data->IsLiveEnabled();
  original_data_.online_xuid =
      string_util::to_hex_string(account_data->GetOnlineXUID());
  original_data_.online_domain =
      std::string(account_data->GetOnlineDomain().data(),
                  account_data->GetOnlineDomain().size());
  original_data_.account_subscription_tier =
      account_data->GetSubscriptionTier();

  // Load GPD settings if signed in
  const auto profile = xam_state->GetUserProfile(xuid_);
  if (profile) {
    // List of settings to load
    const std::array<UserSettingId, 19> kSettingsToLoad = {
        UserSettingId::XPROFILE_GAMER_TYPE,
        UserSettingId::XPROFILE_GAMER_YAXIS_INVERSION,
        UserSettingId::XPROFILE_OPTION_CONTROLLER_VIBRATION,
        UserSettingId::XPROFILE_GAMERCARD_ZONE,
        UserSettingId::XPROFILE_GAMERCARD_REGION,
        UserSettingId::XPROFILE_GAMER_DIFFICULTY,
        UserSettingId::XPROFILE_GAMER_CONTROL_SENSITIVITY,
        UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_FIRST,
        UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_SECOND,
        UserSettingId::XPROFILE_GAMER_ACTION_AUTO_AIM,
        UserSettingId::XPROFILE_GAMER_ACTION_AUTO_CENTER,
        UserSettingId::XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL,
        UserSettingId::XPROFILE_GAMER_RACE_TRANSMISSION,
        UserSettingId::XPROFILE_GAMER_RACE_CAMERA_LOCATION,
        UserSettingId::XPROFILE_GAMER_RACE_BRAKE_CONTROL,
        UserSettingId::XPROFILE_GAMER_RACE_ACCELERATOR_CONTROL,
        UserSettingId::XPROFILE_GAMERCARD_USER_NAME,
        UserSettingId::XPROFILE_GAMERCARD_USER_BIO,
        UserSettingId::XPROFILE_GAMERCARD_MOTTO};

    // Load settings like ImGui does - collect them first without
    // get_host_data()
    std::vector<std::pair<UserSettingId, UserSetting>> loaded_settings;

    for (size_t i = 0; i < kSettingsToLoad.size(); i++) {
      const auto setting_id = kSettingsToLoad[i];

      const auto setting = xam_state->user_tracker()->GetSetting(
          profile, kernel::kDashboardID, static_cast<uint32_t>(setting_id));

      if (setting) {
        loaded_settings.emplace_back(setting_id, setting.value());
      }
    }

    // Now process them outside the loop
    for (size_t i = 0; i < loaded_settings.size(); i++) {
      const auto& [setting_id, setting] = loaded_settings[i];

      auto host_data = setting.get_host_data();

      original_data_.gpd_settings[setting_id] = host_data;
    }

    // Load string settings
    auto load_string_setting = [&](UserSettingId setting_id,
                                   std::string& target) {
      if (original_data_.gpd_settings.contains(setting_id)) {
        target = xe::to_utf8(
            std::get<std::u16string>(original_data_.gpd_settings[setting_id]));
      }
    };

    load_string_setting(UserSettingId::XPROFILE_GAMERCARD_USER_NAME,
                        original_data_.gamer_name);
    load_string_setting(UserSettingId::XPROFILE_GAMERCARD_MOTTO,
                        original_data_.gamer_motto);
    load_string_setting(UserSettingId::XPROFILE_GAMERCARD_USER_BIO,
                        original_data_.gamer_bio);

    // Load profile icon - try personal picture first, then fall back to gamer
    // tile First, try to get the personal picture (if user has set one)
    const auto personal_icon =
        profile->GetProfileIcon(XTileType::kPersonalGamerTile);
    if (!personal_icon.empty()) {
      original_data_.profile_icon.assign(personal_icon.begin(),
                                         personal_icon.end());
    } else {
      // Fall back to regular gamer tile
      const auto gamer_icon = profile->GetProfileIcon(XTileType::kGamerTile);
      original_data_.profile_icon.assign(gamer_icon.begin(), gamer_icon.end());
    }
  }

  // Copy to current data
  current_data_ = original_data_;
}

void ProfileEditorDialogQt::SetupUI() {
  if (!emulator_window_ || !emulator_window_->emulator()) {
    return;
  }

  auto kernel_state = emulator_window_->emulator()->kernel_state();
  auto xam_state = kernel_state ? kernel_state->xam_state() : nullptr;
  auto profile_manager = xam_state ? xam_state->profile_manager() : nullptr;
  const auto profile = xam_state ? xam_state->GetUserProfile(xuid_) : nullptr;

  setWindowTitle(QString("Gamercard Editor - %1")
                     .arg(SafeQString(current_data_.gamertag)));
  setModal(false);  // Non-modal like ProfileDialogQt
  setAttribute(Qt::WA_DeleteOnClose);
  setMinimumSize(900, 700);

  auto* main_layout = new QVBoxLayout(this);

  // Create scroll area
  auto* scroll_area = new QScrollArea(this);
  scroll_area->setWidgetResizable(true);
  scroll_area->setFrameShape(QFrame::NoFrame);

  auto* scroll_widget = new QWidget(scroll_area);
  scroll_area->setWidget(scroll_widget);
  auto* content_layout = new QHBoxLayout(scroll_widget);

  // Left column - Profile Settings
  auto* left_column = new QVBoxLayout();

  // Profile Settings Group
  auto* profile_group = new QGroupBox("Profile Settings");
  auto* profile_layout = new QGridLayout();
  int row = 0;

  // Profile icon
  icon_label_ = new QLabel();
  icon_label_->setFixedSize(64, 64);
  icon_label_->setScaledContents(true);
  icon_label_->setFrameStyle(QFrame::Box);
  LoadProfileIcon();
  profile_layout->addWidget(icon_label_, row, 0, 2, 1);

  change_icon_button_ = new QPushButton("Change Icon");
  change_icon_button_->setEnabled(profile != nullptr &&
                                  !kernel_state->title_id());
  if (kernel_state->title_id()) {
    change_icon_button_->setToolTip(
        "Icon change is disabled when title is running.");
  } else if (!profile) {
    change_icon_button_->setToolTip(
        "Profile must be signed in to change icon.");
  } else {
    change_icon_button_->setToolTip(
        "Provide a PNG image with a resolution of 64x64. Icon will refresh "
        "after relog.");
  }
  connect(change_icon_button_, &QPushButton::clicked, this,
          &ProfileEditorDialogQt::OnChangeIconClicked);
  profile_layout->addWidget(change_icon_button_, row++, 1);

  // Skip another row since icon spans 2 rows
  row++;

  // Gamertag
  profile_layout->addWidget(new QLabel("Gamertag:"), row, 0);
  gamertag_edit_ = new QLineEdit(SafeQString(current_data_.gamertag));
  gamertag_edit_->setMaxLength(15);
  connect(gamertag_edit_, &QLineEdit::textChanged, this,
          &ProfileEditorDialogQt::UpdateGamertagValidation);
  profile_layout->addWidget(gamertag_edit_, row++, 1);

  gamertag_validation_label_ = new QLabel();
  gamertag_validation_label_->setStyleSheet("QLabel { color: red; }");
  gamertag_validation_label_->setVisible(false);
  profile_layout->addWidget(gamertag_validation_label_, row++, 1);

  // Gamer Name
  profile_layout->addWidget(new QLabel("Gamer Name:"), row, 0);
  gamer_name_edit_ = new QLineEdit(SafeQString(current_data_.gamer_name));
  gamer_name_edit_->setMaxLength(130);  // 0x104 bytes = 130 UTF-16 characters
  gamer_name_edit_->setEnabled(profile != nullptr);
  profile_layout->addWidget(gamer_name_edit_, row++, 1);

  // Gamer Motto
  profile_layout->addWidget(new QLabel("Gamer Motto:"), row, 0);
  gamer_motto_edit_ = new QLineEdit(SafeQString(current_data_.gamer_motto));
  gamer_motto_edit_->setMaxLength(22);  // 0x2C bytes = 22 UTF-16 characters
  gamer_motto_edit_->setEnabled(profile != nullptr);
  profile_layout->addWidget(gamer_motto_edit_, row++, 1);

  // Gamer Bio
  profile_layout->addWidget(new QLabel("Gamer Bio:"), row, 0);
  gamer_bio_edit_ = new QTextEdit(SafeQString(current_data_.gamer_bio));
  gamer_bio_edit_->setEnabled(profile != nullptr);
  gamer_bio_edit_->setMaximumHeight(100);
  // Limit bio to 500 characters (0x3E8 bytes = 500 UTF-16 characters)
  connect(gamer_bio_edit_, &QTextEdit::textChanged, [this]() {
    QString text = gamer_bio_edit_->toPlainText();
    if (text.length() > 500) {
      text.truncate(500);
      gamer_bio_edit_->setPlainText(text);
      // Move cursor to end
      QTextCursor cursor = gamer_bio_edit_->textCursor();
      cursor.movePosition(QTextCursor::End);
      gamer_bio_edit_->setTextCursor(cursor);
    }
  });
  profile_layout->addWidget(gamer_bio_edit_, row++, 1);

  // Language
  profile_layout->addWidget(new QLabel("Language:"), row, 0);
  language_combo_ = new QComboBox();
  for (size_t i = 0; i < std::size(kLanguageNames); i++) {
    if (kLanguageNames[i]) {
      language_combo_->addItem(kLanguageNames[i], static_cast<int>(i));
    }
  }
  language_combo_->setCurrentIndex(
      language_combo_->findData(static_cast<int>(current_data_.language)));
  profile_layout->addWidget(language_combo_, row++, 1);

  // Country
  profile_layout->addWidget(new QLabel("Country:"), row, 0);
  country_combo_ = new QComboBox();
  for (size_t i = 0; i < std::size(kCountryNames); i++) {
    if (kCountryNames[i]) {
      country_combo_->addItem(kCountryNames[i], static_cast<int>(i));
    }
  }
  country_combo_->setCurrentIndex(
      country_combo_->findData(static_cast<int>(current_data_.country)));
  profile_layout->addWidget(country_combo_, row++, 1);

  profile_group->setLayout(profile_layout);
  left_column->addWidget(profile_group);

  // Online Profile Settings Group
  auto* online_group = new QGroupBox("Online Profile Settings");
  auto* online_layout = new QGridLayout();
  row = 0;

  // Live Enabled
  live_enabled_checkbox_ = new QCheckBox("Live Enabled");
  live_enabled_checkbox_->setChecked(current_data_.is_live_enabled);
  online_layout->addWidget(live_enabled_checkbox_, row++, 0, 1, 2);

  // Online XUID (read-only)
  online_layout->addWidget(new QLabel("Online XUID:"), row, 0);
  online_xuid_edit_ = new QLineEdit(SafeQString(current_data_.online_xuid));
  online_xuid_edit_->setReadOnly(true);
  online_layout->addWidget(online_xuid_edit_, row++, 1);

  // Online Domain (read-only)
  online_layout->addWidget(new QLabel("Online Domain:"), row, 0);
  online_domain_edit_ = new QLineEdit(SafeQString(current_data_.online_domain));
  online_domain_edit_->setReadOnly(true);
  online_layout->addWidget(online_domain_edit_, row++, 1);

  // Gamer Zone
  online_layout->addWidget(new QLabel("Gamer Zone:"), row, 0);
  gamer_zone_combo_ = new QComboBox();
  for (size_t i = 0; i < std::size(kGamerZoneNames); i++) {
    if (kGamerZoneNames[i]) {
      gamer_zone_combo_->addItem(kGamerZoneNames[i], static_cast<int>(i));
    }
  }
  if (current_data_.gpd_settings.contains(
          UserSettingId::XPROFILE_GAMERCARD_ZONE)) {
    int32_t zone_value = std::get<int32_t>(
        current_data_.gpd_settings[UserSettingId::XPROFILE_GAMERCARD_ZONE]);
    gamer_zone_combo_->setCurrentIndex(gamer_zone_combo_->findData(zone_value));
  }
  gamer_zone_combo_->setEnabled(current_data_.is_live_enabled &&
                                profile != nullptr);
  online_layout->addWidget(gamer_zone_combo_, row++, 1);

  // Subscription Tier
  online_layout->addWidget(new QLabel("Subscription Tier:"), row, 0);
  subscription_tier_combo_ = new QComboBox();
  for (size_t i = 0; i < std::size(kSubscriptionTierNames); i++) {
    if (kSubscriptionTierNames[i]) {
      subscription_tier_combo_->addItem(kSubscriptionTierNames[i],
                                        static_cast<int>(i));
    }
  }
  subscription_tier_combo_->setCurrentIndex(subscription_tier_combo_->findData(
      static_cast<int>(current_data_.account_subscription_tier)));
  subscription_tier_combo_->setEnabled(current_data_.is_live_enabled);
  online_layout->addWidget(subscription_tier_combo_, row++, 1);

  // Connect live enabled checkbox to enable/disable dependent controls
  connect(live_enabled_checkbox_, &QCheckBox::toggled, [this](bool checked) {
    gamer_zone_combo_->setEnabled(checked);
    subscription_tier_combo_->setEnabled(checked);
  });

  online_group->setLayout(online_layout);
  left_column->addWidget(online_group);

  left_column->addStretch();
  content_layout->addLayout(left_column);

  // Right column - Game Settings
  auto* right_column = new QVBoxLayout();

  // Game Settings Group
  auto* game_group = new QGroupBox("Game Settings");
  auto* game_layout = new QGridLayout();
  row = 0;

  auto add_combo_setting = [&](const char* label, QComboBox** combo,
                               const char* const* options, size_t option_count,
                               UserSettingId setting_id) {
    game_layout->addWidget(new QLabel(label), row, 0);
    *combo = new QComboBox();
    for (size_t i = 0; i < option_count; i++) {
      if (options[i]) {
        (*combo)->addItem(options[i], static_cast<int>(i));
      }
    }
    if (current_data_.gpd_settings.contains(setting_id)) {
      int32_t value = std::get<int32_t>(current_data_.gpd_settings[setting_id]);
      (*combo)->setCurrentIndex((*combo)->findData(value));
    }
    (*combo)->setEnabled(profile != nullptr);
    game_layout->addWidget(*combo, row++, 1);
  };

  add_combo_setting("Difficulty:", &difficulty_combo_, kGamerDifficultyOptions,
                    std::size(kGamerDifficultyOptions),
                    UserSettingId::XPROFILE_GAMER_DIFFICULTY);
  add_combo_setting("Controller Vibration:", &controller_vibration_combo_,
                    kControllerVibrationOptions,
                    std::size(kControllerVibrationOptions),
                    UserSettingId::XPROFILE_OPTION_CONTROLLER_VIBRATION);
  add_combo_setting("Control Sensitivity:", &control_sensitivity_combo_,
                    kControlSensitivityOptions,
                    std::size(kControlSensitivityOptions),
                    UserSettingId::XPROFILE_GAMER_CONTROL_SENSITIVITY);
  add_combo_setting("Favorite Color (First):", &preferred_color_first_combo_,
                    kPreferredColorNames, std::size(kPreferredColorNames),
                    UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_FIRST);
  add_combo_setting("Favorite Color (Second):", &preferred_color_second_combo_,
                    kPreferredColorNames, std::size(kPreferredColorNames),
                    UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_SECOND);

  game_group->setLayout(game_layout);
  right_column->addWidget(game_group);

  // Action Games Settings Group
  auto* action_group = new QGroupBox("Action Games Settings");
  auto* action_layout = new QGridLayout();
  row = 0;

  auto add_action_combo_setting = [&](const char* label, QComboBox** combo,
                                      const char* const* options,
                                      size_t option_count,
                                      UserSettingId setting_id) {
    action_layout->addWidget(new QLabel(label), row, 0);
    *combo = new QComboBox();
    for (size_t i = 0; i < option_count; i++) {
      if (options[i]) {
        (*combo)->addItem(options[i], static_cast<int>(i));
      }
    }
    if (current_data_.gpd_settings.contains(setting_id)) {
      int32_t value = std::get<int32_t>(current_data_.gpd_settings[setting_id]);
      (*combo)->setCurrentIndex((*combo)->findData(value));
    }
    (*combo)->setEnabled(profile != nullptr);
    action_layout->addWidget(*combo, row++, 1);
  };

  add_action_combo_setting("Y-axis Inversion:", &yaxis_inversion_combo_,
                           kYAxisInversionOptions,
                           std::size(kYAxisInversionOptions),
                           UserSettingId::XPROFILE_GAMER_YAXIS_INVERSION);
  add_action_combo_setting("Auto Aim:", &auto_aim_combo_, kAutoAimOptions,
                           std::size(kAutoAimOptions),
                           UserSettingId::XPROFILE_GAMER_ACTION_AUTO_AIM);
  add_action_combo_setting("Auto Center:", &auto_center_combo_,
                           kAutoCenterOptions, std::size(kAutoCenterOptions),
                           UserSettingId::XPROFILE_GAMER_ACTION_AUTO_CENTER);
  add_action_combo_setting(
      "Movement Control:", &movement_control_combo_, kMovementControlOptions,
      std::size(kMovementControlOptions),
      UserSettingId::XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL);

  action_group->setLayout(action_layout);
  right_column->addWidget(action_group);

  // Racing Games Settings Group
  auto* racing_group = new QGroupBox("Racing Games Settings");
  auto* racing_layout = new QGridLayout();
  row = 0;

  auto add_racing_combo_setting = [&](const char* label, QComboBox** combo,
                                      const char* const* options,
                                      size_t option_count,
                                      UserSettingId setting_id) {
    racing_layout->addWidget(new QLabel(label), row, 0);
    *combo = new QComboBox();
    for (size_t i = 0; i < option_count; i++) {
      if (options[i]) {
        (*combo)->addItem(options[i], static_cast<int>(i));
      }
    }
    if (current_data_.gpd_settings.contains(setting_id)) {
      int32_t value = std::get<int32_t>(current_data_.gpd_settings[setting_id]);
      (*combo)->setCurrentIndex((*combo)->findData(value));
    }
    (*combo)->setEnabled(profile != nullptr);
    racing_layout->addWidget(*combo, row++, 1);
  };

  add_racing_combo_setting("Transmission:", &transmission_combo_,
                           kTransmissionOptions,
                           std::size(kTransmissionOptions),
                           UserSettingId::XPROFILE_GAMER_RACE_TRANSMISSION);
  add_racing_combo_setting("Camera Location:", &camera_location_combo_,
                           kCameraLocationOptions,
                           std::size(kCameraLocationOptions),
                           UserSettingId::XPROFILE_GAMER_RACE_CAMERA_LOCATION);
  add_racing_combo_setting("Brake Control:", &brake_control_combo_,
                           kBrakeControlOptions,
                           std::size(kBrakeControlOptions),
                           UserSettingId::XPROFILE_GAMER_RACE_BRAKE_CONTROL);
  add_racing_combo_setting(
      "Accelerator Control:", &accelerator_control_combo_,
      kAcceleratorControlOptions, std::size(kAcceleratorControlOptions),
      UserSettingId::XPROFILE_GAMER_RACE_ACCELERATOR_CONTROL);

  racing_group->setLayout(racing_layout);
  right_column->addWidget(racing_group);

  right_column->addStretch();
  content_layout->addLayout(right_column);

  scroll_area->setWidget(scroll_widget);
  main_layout->addWidget(scroll_area);

  // Buttons
  auto* button_layout = new QHBoxLayout();
  button_layout->addStretch();

  save_button_ = new QPushButton("Save");
  connect(save_button_, &QPushButton::clicked, this,
          &ProfileEditorDialogQt::OnSaveClicked);
  button_layout->addWidget(save_button_);

  cancel_button_ = new QPushButton("Cancel");
  connect(cancel_button_, &QPushButton::clicked, this,
          &ProfileEditorDialogQt::OnCancelClicked);
  button_layout->addWidget(cancel_button_);

  main_layout->addLayout(button_layout);
}

void ProfileEditorDialogQt::LoadProfileIcon() {
  if (current_data_.profile_icon.empty()) {
    icon_label_->setText("No Icon");
    return;
  }

  QImage image;
  if (image.loadFromData(current_data_.profile_icon.data(),
                         static_cast<int>(current_data_.profile_icon.size()))) {
    icon_label_->setPixmap(QPixmap::fromImage(image));
  } else {
    icon_label_->setText("Invalid Icon");
  }
}

bool ProfileEditorDialogQt::ValidateGamertagInput(const QString& text) {
  std::string gamertag = SafeStdString(text);
  return ProfileManager::IsGamertagValid(gamertag);
}

void ProfileEditorDialogQt::UpdateGamertagValidation() {
  // Check if widgets are initialized
  if (!gamertag_edit_ || !gamertag_validation_label_ || !save_button_) {
    return;
  }

  QString gamertag = gamertag_edit_->text();
  bool is_valid = ValidateGamertagInput(gamertag);

  if (!is_valid && !gamertag.isEmpty()) {
    gamertag_validation_label_->setText("Invalid gamertag");
    gamertag_validation_label_->setVisible(true);
    save_button_->setEnabled(false);
  } else {
    gamertag_validation_label_->setVisible(false);
    save_button_->setEnabled(true);
  }
}

void ProfileEditorDialogQt::OnChangeIconClicked() {
  QString file_path = QFileDialog::getOpenFileName(
      this, "Select PNG Image", QString(), "PNG Images (*.png);;All Files (*)");

  if (file_path.isEmpty()) {
    return;
  }

  std::filesystem::path path = SafeStdString(file_path);

  if (!IsFilePngImage(path)) {
    QMessageBox::warning(this, "Invalid File",
                         "Selected file is not a valid PNG image.");
    return;
  }

  const auto res = GetImageResolution(path);
  if (res != kProfileIconSizeSmall && res != kProfileIconSize) {
    QMessageBox::warning(
        this, "Invalid Resolution",
        QString("Profile icon must be 64x64 or 32x32 pixels. Selected image "
                "is %1x%2.")
            .arg(res.first)
            .arg(res.second));
    return;
  }

  current_data_.profile_icon = ReadPngFromFile(path);
  LoadProfileIcon();
}

void ProfileEditorDialogQt::OnSaveClicked() {
  SaveProfileData();
  accept();
}

void ProfileEditorDialogQt::OnCancelClicked() { reject(); }

void ProfileEditorDialogQt::SaveProfileData() {
  if (!emulator_window_ || !emulator_window_->emulator()) {
    return;
  }

  auto kernel_state = emulator_window_->emulator()->kernel_state();
  if (!kernel_state) {
    return;
  }

  auto xam_state = kernel_state->xam_state();
  if (!xam_state) {
    return;
  }

  auto profile_manager = xam_state->profile_manager();
  if (!profile_manager) {
    return;
  }

  const auto profile = xam_state->GetUserProfile(xuid_);
  const bool is_signed_in = (profile != nullptr);

  // Save account data
  const auto account_original = *profile_manager->GetAccount(xuid_);
  auto account = account_original;

  // Update gamertag
  std::string gamertag_str = SafeStdString(gamertag_edit_->text());
  std::u16string gamertag = xe::to_utf16(gamertag_str);
  string_util::copy_and_swap_truncating(account.gamertag, gamertag,
                                        sizeof(account.gamertag));

  // Update language and country
  account.SetLanguage(
      static_cast<XLanguage>(language_combo_->currentData().toInt()));
  account.SetCountry(
      static_cast<XOnlineCountry>(country_combo_->currentData().toInt()));

  // Update live enabled and subscription tier
  account.ToggleLiveFlag(live_enabled_checkbox_->isChecked());
  account.SetSubscriptionTier(
      static_cast<X_XAMACCOUNTINFO::AccountSubscriptionTier>(
          subscription_tier_combo_->currentData().toInt()));

  // Save account if changed
  if (std::memcmp(&account, &account_original, sizeof(X_XAMACCOUNTINFO)) != 0) {
    if (!is_signed_in) {
      profile_manager->MountProfile(xuid_);
    }
    profile_manager->UpdateAccount(xuid_, &account);
    if (!is_signed_in) {
      profile_manager->DismountProfile(xuid_);
    }
  }

  // Save profile icon if changed
  if (current_data_.profile_icon != original_data_.profile_icon &&
      !current_data_.profile_icon.empty()) {
    xam_state->user_tracker()->UpdateUserIcon(
        xuid_,
        {current_data_.profile_icon.data(), current_data_.profile_icon.size()});
  }

  // Save GPD settings if signed in
  if (is_signed_in) {
    // Helper function to save string settings
    auto save_string_setting = [&](UserSettingId setting_id,
                                   const QString& text,
                                   size_t max_utf16_chars) {
      // Convert QString to std::u16string using xe::to_utf16 for proper
      // conversion
      std::string utf8_text = SafeStdString(text);
      std::u16string new_value = xe::to_utf16(utf8_text);

      // Truncate if exceeds max length (in UTF-16 characters)
      if (new_value.length() > max_utf16_chars) {
        new_value.resize(max_utf16_chars);
      }

      // Swap to big-endian for GPD storage (Xbox 360 format)
      std::u16string swapped_value;
      swapped_value.reserve(new_value.size());
      for (const auto& ch : new_value) {
        swapped_value.push_back(xe::byte_swap(ch));
      }

      // Check if the value has changed (compare with swapped value since that's
      // what's stored)
      if (current_data_.gpd_settings.contains(setting_id)) {
        if (std::holds_alternative<std::u16string>(
                current_data_.gpd_settings[setting_id])) {
          std::u16string current_value =
              std::get<std::u16string>(current_data_.gpd_settings[setting_id]);
          if (current_value == swapped_value) {
            return;  // No change
          }
        }
      }

      UserSetting updated_setting(setting_id, swapped_value);
      xam_state->user_tracker()->UpsertSetting(xuid_, kernel::kDashboardID,
                                               &updated_setting);
    };

    // Save text field settings
    save_string_setting(UserSettingId::XPROFILE_GAMERCARD_USER_NAME,
                        gamer_name_edit_->text(),
                        130);  // 0x104 bytes = 130 UTF-16 chars
    save_string_setting(UserSettingId::XPROFILE_GAMERCARD_MOTTO,
                        gamer_motto_edit_->text(),
                        22);  // 0x2C bytes = 22 UTF-16 chars
    save_string_setting(UserSettingId::XPROFILE_GAMERCARD_USER_BIO,
                        gamer_bio_edit_->toPlainText(),
                        500);  // 0x3E8 bytes = 500 UTF-16 chars

    auto save_setting = [&](UserSettingId setting_id, QComboBox* combo) {
      if (!combo->isEnabled()) {
        return;
      }
      int32_t value = combo->currentData().toInt();
      if (current_data_.gpd_settings.contains(setting_id)) {
        if (std::get<int32_t>(current_data_.gpd_settings[setting_id]) ==
            value) {
          return;  // No change
        }
      }
      UserSetting updated_setting(setting_id, value);
      xam_state->user_tracker()->UpsertSetting(xuid_, kernel::kDashboardID,
                                               &updated_setting);
    };

    // Save all combo box settings
    save_setting(UserSettingId::XPROFILE_GAMER_DIFFICULTY, difficulty_combo_);
    save_setting(UserSettingId::XPROFILE_OPTION_CONTROLLER_VIBRATION,
                 controller_vibration_combo_);
    save_setting(UserSettingId::XPROFILE_GAMER_CONTROL_SENSITIVITY,
                 control_sensitivity_combo_);
    save_setting(UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_FIRST,
                 preferred_color_first_combo_);
    save_setting(UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_SECOND,
                 preferred_color_second_combo_);
    save_setting(UserSettingId::XPROFILE_GAMER_YAXIS_INVERSION,
                 yaxis_inversion_combo_);
    save_setting(UserSettingId::XPROFILE_GAMER_ACTION_AUTO_AIM,
                 auto_aim_combo_);
    save_setting(UserSettingId::XPROFILE_GAMER_ACTION_AUTO_CENTER,
                 auto_center_combo_);
    save_setting(UserSettingId::XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL,
                 movement_control_combo_);
    save_setting(UserSettingId::XPROFILE_GAMER_RACE_TRANSMISSION,
                 transmission_combo_);
    save_setting(UserSettingId::XPROFILE_GAMER_RACE_CAMERA_LOCATION,
                 camera_location_combo_);
    save_setting(UserSettingId::XPROFILE_GAMER_RACE_BRAKE_CONTROL,
                 brake_control_combo_);
    save_setting(UserSettingId::XPROFILE_GAMER_RACE_ACCELERATOR_CONTROL,
                 accelerator_control_combo_);
    save_setting(UserSettingId::XPROFILE_GAMERCARD_ZONE, gamer_zone_combo_);

    // Broadcast profile settings changed notification
    const uint32_t user_index =
        profile_manager->GetUserIndexAssignedToProfile(xuid_);
    if (user_index != XUserIndexAny) {
      kernel_state->BroadcastNotification(
          kXNotificationSystemProfileSettingChanged, 0xF);
    }
  }
}

}  // namespace app
}  // namespace xe
