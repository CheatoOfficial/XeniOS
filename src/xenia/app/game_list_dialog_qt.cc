/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/game_list_dialog_qt.h"

#include <QCursor>
#include <QDesktopServices>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHeaderView>
#include <QImage>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QScrollBar>
#include <QUrl>
#include <chrono>
#include <fstream>
#include <regex>
#include <thread>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/app/emulator_window.h"
#include "xenia/app/profile_dialog_qt.h"
#include "xenia/app/profile_editor_dialog_qt.h"
#include "xenia/base/chrono.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"
#include "xenia/base/system.h"
#include "xenia/base/utf8.h"
#include "xenia/emulator.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/title_id_utils.h"
#include "xenia/kernel/xam/profile_manager.h"
#include "xenia/kernel/xam/ui/gamercard_ui.h"
#include "xenia/kernel/xam/ui/title_info_ui.h"
#include "xenia/kernel/xam/user_tracker.h"
#include "xenia/kernel/xam/xam_state.h"
#include "xenia/kernel/xam/xdbf/gpd_info_profile.h"
#include "xenia/kernel/xam/xdbf/gpd_info_title.h"
#include "xenia/ui/achievements_dialog_qt.h"

namespace xe {
namespace app {

GameListDialogQt::GameListDialogQt(QWidget* parent,
                                   EmulatorWindow* emulator_window)
    : QWidget(parent), emulator_window_(emulator_window) {
  SetupUI();
  LoadGameList();
  TryLoadIcons();

  // Start timer to monitor game state and profile state
  game_state_timer_ = new QTimer(this);
  connect(game_state_timer_, &QTimer::timeout, this, [this]() {
    UpdatePlayButtonState();
    UpdateProfileButtonState();
  });
  game_state_timer_->start(500);  // Check every 500ms
}

GameListDialogQt::~GameListDialogQt() {
  if (game_state_timer_) {
    game_state_timer_->stop();
  }
  // Cleanup is handled automatically by Qt
}

void GameListDialogQt::SetupUI() {
  // No window title, modal, or size settings - widget fills parent
  setAttribute(Qt::WA_DeleteOnClose);

  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(0);

  // Icon toolbar
  auto* toolbar_widget = new QWidget(this);
  auto* toolbar_layout = new QHBoxLayout(toolbar_widget);
  toolbar_layout->setContentsMargins(10, 10, 10, 10);
  toolbar_layout->setSpacing(20);

  // Create a layout for each button with icon and text separately

  // Open button
  auto* open_container = new QWidget(this);
  auto* open_layout = new QVBoxLayout(open_container);
  open_layout->setContentsMargins(0, 0, 0, 0);
  open_layout->setSpacing(5);

  auto* open_button = new QToolButton(this);
  open_button->setIcon(QIcon(":/xenia/folder-icon.png"));
  open_button->setIconSize(QSize(64, 64));
  open_button->setMinimumSize(100, 80);
  open_button->setMaximumSize(100, 80);
  connect(open_button, &QToolButton::clicked, [this]() {
    if (emulator_window_) {
      emulator_window_->FileOpen();
    }
  });

  auto* open_label = new QLabel("Open", this);
  open_label->setAlignment(Qt::AlignCenter);
  open_label->setFixedHeight(20);

  open_layout->addWidget(open_button);
  open_layout->addWidget(open_label);
  toolbar_layout->addWidget(open_container);

  // Play button
  auto* play_container = new QWidget(this);
  auto* play_layout = new QVBoxLayout(play_container);
  play_layout->setContentsMargins(0, 0, 0, 0);
  play_layout->setSpacing(5);

  // Use a button with an icon
  play_button_ = new QToolButton(this);
  play_button_->setIcon(QIcon(":/xenia/play-icon.png"));
  play_button_->setIconSize(QSize(64, 64));
  play_button_->setMinimumSize(100, 80);
  play_button_->setMaximumSize(100, 80);
  play_button_->setEnabled(false);

  // Create opacity effect for greying out
  play_opacity_ = new QGraphicsOpacityEffect(play_button_);
  play_opacity_->setOpacity(0.4);
  play_button_->setGraphicsEffect(play_opacity_);

  connect(play_button_, &QToolButton::clicked, this,
          &GameListDialogQt::OnPlayClicked);

  play_label_ = new QLabel("Play", this);
  play_label_->setAlignment(Qt::AlignCenter);
  QFont text_font = play_label_->font();
  text_font.setPointSize(10);
  play_label_->setFont(text_font);
  open_label->setFont(text_font);  // Apply to open label too
  play_label_->setFixedHeight(20);

  // Create opacity effect for label
  play_label_opacity_ = new QGraphicsOpacityEffect(play_label_);
  play_label_opacity_->setOpacity(0.4);
  play_label_->setGraphicsEffect(play_label_opacity_);

  play_layout->addWidget(play_button_);
  play_layout->addWidget(play_label_);
  toolbar_layout->addWidget(play_container);

  // Settings button
  auto* settings_container = new QWidget(this);
  auto* settings_layout = new QVBoxLayout(settings_container);
  settings_layout->setContentsMargins(0, 0, 0, 0);
  settings_layout->setSpacing(5);

  settings_button_ = new QToolButton(this);
  settings_button_->setIcon(QIcon(":/xenia/settings-icon.png"));
  settings_button_->setIconSize(QSize(64, 64));
  settings_button_->setMinimumSize(100, 80);
  settings_button_->setMaximumSize(100, 80);
  connect(settings_button_, &QToolButton::clicked, this,
          &GameListDialogQt::OnSettingsClicked);

  auto* settings_label = new QLabel("Config", this);
  settings_label->setAlignment(Qt::AlignCenter);
  settings_label->setFont(text_font);
  settings_label->setFixedHeight(20);

  settings_layout->addWidget(settings_button_);
  settings_layout->addWidget(settings_label);
  toolbar_layout->addWidget(settings_container);

  // Spacer to push profile button to the right
  toolbar_layout->addStretch();

  // Profile button
  auto* profile_container = new QWidget(this);
  auto* profile_layout = new QVBoxLayout(profile_container);
  profile_layout->setContentsMargins(0, 0, 0, 0);
  profile_layout->setSpacing(5);

  profile_button_ = new QToolButton(this);
  profile_button_->setIcon(QIcon(":/xenia/user-icon.png"));
  profile_button_->setIconSize(QSize(64, 64));
  profile_button_->setMinimumSize(100, 80);
  profile_button_->setMaximumSize(100, 80);
  profile_button_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(profile_button_, &QToolButton::clicked, this,
          &GameListDialogQt::OnProfileClicked);
  connect(profile_button_, &QToolButton::customContextMenuRequested, this,
          &GameListDialogQt::OnProfileContextMenu);

  // Question mark overlay for logged out state
  profile_question_label_ = new QLabel("?", profile_button_);
  profile_question_label_->setAlignment(Qt::AlignCenter);
  QFont question_font = profile_question_label_->font();
  question_font.setPointSize(32);
  question_font.setBold(true);
  profile_question_label_->setFont(question_font);
  profile_question_label_->setGeometry(0, 0, 100, 80);
  profile_question_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
  profile_question_label_->setStyleSheet(
      "color: white; background-color: rgba(0, 0, 0, 150);");
  profile_question_label_->setVisible(
      true);  // Will be controlled by UpdateProfileButtonState

  profile_label_ = new QLabel("Logged Out", this);
  profile_label_->setAlignment(Qt::AlignCenter);
  profile_label_->setFont(text_font);
  profile_label_->setFixedHeight(20);

  profile_layout->addWidget(profile_button_);
  profile_layout->addWidget(profile_label_);
  toolbar_layout->addWidget(profile_container);

  main_layout->addWidget(toolbar_widget);

  // Search box
  search_box_ = new QLineEdit(this);
  search_box_->setPlaceholderText("Search games...");
  search_box_->setClearButtonEnabled(true);
  QFont search_font = search_box_->font();
  search_font.setPointSize(16);
  search_box_->setFont(search_font);
  connect(search_box_, &QLineEdit::textChanged, this,
          &GameListDialogQt::OnFilterTextChanged);

  // Only show search box if there are more than 5 games
  search_box_->setVisible(false);

  main_layout->addWidget(search_box_);

  // Custom header widget to match row layout
  auto* header_widget = new QWidget(this);
  auto* header_layout = new QHBoxLayout(header_widget);
  header_layout->setContentsMargins(10, 5, 10, 5);
  header_layout->setSpacing(15);

  auto* icon_header = new QLabel("Icon", this);
  icon_header->setMinimumWidth(80);
  icon_header->setMaximumWidth(80);
  QFont header_font = icon_header->font();
  header_font.setBold(true);
  icon_header->setFont(header_font);
  header_layout->addWidget(icon_header);

  auto* title_header = new QLabel("Title", this);
  title_header->setFont(header_font);
  header_layout->addWidget(title_header, 1);

  auto* last_played_header = new QLabel("Last Played", this);
  last_played_header->setFont(header_font);
  last_played_header->setMinimumWidth(200);
  last_played_header->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  header_layout->addWidget(last_played_header);

  main_layout->addWidget(header_widget);

  // Table widget
  table_widget_ = new QTableWidget(this);
  table_widget_->setColumnCount(1);
  table_widget_->horizontalHeader()->setVisible(false);
  table_widget_->horizontalHeader()->setStretchLastSection(true);
  table_widget_->verticalHeader()->setVisible(false);
  table_widget_->setShowGrid(false);
  table_widget_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_widget_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_widget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_widget_->setContextMenuPolicy(Qt::CustomContextMenu);
  table_widget_->setMouseTracking(true);

  // Set Xbox green theme for table
  table_widget_->setStyleSheet(
      "QTableWidget::item:hover { background-color: transparent; }"
      "QTableWidget::item:selected { background-color: rgba(16, 124, 16, 120); "
      "}");

  // Auto-hide scrollbar - only show when hovering or scrolling
  table_widget_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  table_widget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  // Install event filter to detect scrolling
  table_widget_->viewport()->installEventFilter(this);

  // Create timer for hiding scrollbar after scrolling stops
  scrollbar_hide_timer_ = new QTimer(this);
  scrollbar_hide_timer_->setSingleShot(true);
  scrollbar_hide_timer_->setInterval(1000);  // Hide after 1 second
  connect(scrollbar_hide_timer_, &QTimer::timeout, this,
          &GameListDialogQt::HideScrollbar);

  // Initialize scrollbar to hidden state
  HideScrollbar();

  connect(table_widget_, &QTableWidget::cellDoubleClicked, this,
          &GameListDialogQt::OnGameDoubleClicked);
  connect(table_widget_, &QTableWidget::customContextMenuRequested, this,
          &GameListDialogQt::OnGameRightClicked);
  connect(table_widget_, &QTableWidget::itemSelectionChanged, this,
          &GameListDialogQt::OnSelectionChanged);

  main_layout->addWidget(table_widget_);
}

void GameListDialogQt::LoadGameList() {
  game_entries_.clear();
  title_icons_.clear();

  if (!emulator_window_ || !emulator_window_->emulator()) {
    PopulateTable();
    return;
  }

  auto kernel_state = emulator_window_->emulator()->kernel_state();
  if (!kernel_state) {
    PopulateTable();
    return;
  }

  auto xam_state = kernel_state->xam_state();
  if (!xam_state) {
    PopulateTable();
    return;
  }

  auto profile_manager = xam_state->profile_manager();
  if (!profile_manager) {
    PopulateTable();
    return;
  }

  // Phase 1: Load game metadata from dashboard GPDs directly
  // This works even without a logged-in profile
  std::map<uint32_t, GameListEntry> titles_by_id;

  // Scan all profile directories for dashboard GPDs
  auto content_root = emulator_window_->emulator()->content_root();
  auto profiles_directory = xe::filesystem::FilterByName(
      xe::filesystem::ListDirectories(content_root),
      std::regex("[0-9A-F]{16}"));

  for (const auto& profile_dir : profiles_directory) {
    const std::string profile_xuid = xe::path_to_utf8(profile_dir.name);
    if (profile_xuid == fmt::format("{:016X}", 0)) {
      continue;  // Skip shared content directory
    }

    // Construct path to dashboard GPD
    std::filesystem::path dashboard_gpd_path =
        profile_dir.path / profile_dir.name / kernel::xam::kDashboardStringID /
        fmt::format("{:08X}", static_cast<uint32_t>(XContentType::kProfile)) /
        profile_dir.name / fmt::format("{:08X}.gpd", kernel::kDashboardID);

    if (!std::filesystem::exists(dashboard_gpd_path)) {
      continue;
    }

    // Read dashboard GPD file directly
    std::ifstream file(dashboard_gpd_path, std::ios::binary);
    if (!file.is_open()) {
      continue;
    }

    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> gpd_data(file_size);
    file.read(reinterpret_cast<char*>(gpd_data.data()), file_size);
    file.close();

    // Parse dashboard GPD
    kernel::xam::GpdInfoProfile dashboard_gpd(gpd_data);
    if (!dashboard_gpd.IsValid()) {
      continue;
    }

    // Extract title information from this dashboard GPD
    auto titles_info = dashboard_gpd.GetTitlesInfo();
    for (const auto& title_info : titles_info) {
      uint32_t title_id = title_info->title_id;

      // Get last played time for this profile
      time_t last_played = 0;
      if (title_info->last_played.is_valid()) {
        auto last_played_tp = chrono::WinSystemClock::to_sys(
            title_info->last_played.to_time_point());
        last_played = std::chrono::system_clock::to_time_t(last_played_tp);
      }

      // Check if we already have this title
      if (titles_by_id.find(title_id) != titles_by_id.end()) {
        // Update with most recent timestamp
        if (last_played > titles_by_id[title_id].last_run_time) {
          titles_by_id[title_id].last_run_time = last_played;
        }
        continue;
      }

      // Get title name
      std::string title_name =
          xe::to_utf8(dashboard_gpd.GetTitleName(title_id));
      // Remove null terminator if present
      if (!title_name.empty() && title_name.back() == '\0') {
        title_name.pop_back();
      }

      // Get file path(s) from dashboard GPD
      std::filesystem::path path_to_file;
      auto gpd_path = dashboard_gpd.GetTitlePath(title_id);
      if (gpd_path.has_value()) {
        path_to_file = *gpd_path;
      }

      // Check if there are multiple discs
      auto all_paths = dashboard_gpd.GetTitlePaths(title_id);
      if (all_paths.size() > 1) {
        XELOGI("Title {:08X} has {} discs", title_id, all_paths.size());
      }

      // Add to our map (without icon for now)
      titles_by_id[title_id] = {title_name, path_to_file, last_played, title_id,
                                std::vector<uint8_t>()};
    }
  }

  // Convert map to vector and sort by last played time (most recent first)
  for (auto& [title_id, title] : titles_by_id) {
    game_entries_.push_back(std::move(title));
  }

  std::sort(game_entries_.begin(), game_entries_.end(),
            [](const GameListEntry& a, const GameListEntry& b) {
              return a.last_run_time > b.last_run_time;
            });

  // Show/hide search box based on number of entries
  if (search_box_) {
    search_box_->setVisible(game_entries_.size() > 5);
  }

  // Phase 2: Load icons if a profile is logged in
  TryLoadIcons();
  PopulateTable();
}

void GameListDialogQt::TryLoadIcons() {
  // Icons and achievement data are only loaded if a user is logged in
  if (!emulator_window_ || !emulator_window_->emulator()) {
    has_logged_in_profile_ = false;
    return;
  }

  auto kernel_state = emulator_window_->emulator()->kernel_state();
  if (!kernel_state) {
    has_logged_in_profile_ = false;
    return;
  }

  auto xam_state = kernel_state->xam_state();
  if (!xam_state) {
    has_logged_in_profile_ = false;
    return;
  }

  auto profile_manager = xam_state->profile_manager();
  if (!profile_manager) {
    has_logged_in_profile_ = false;
    return;
  }

  // Check if any profile is logged in
  has_logged_in_profile_ = false;
  for (uint8_t user_index = 0; user_index < 4; user_index++) {
    const auto profile = profile_manager->GetProfile(user_index);
    if (profile) {
      has_logged_in_profile_ = true;

      // Load icons, achievement data, and profile-specific timestamps for all
      // game entries from this profile's title GPDs
      XELOGI(
          "Loading icons, achievement data, and timestamps for {} game entries "
          "from profile "
          "{:016X}",
          game_entries_.size(), profile->xuid());

      // Get the profile's dashboard GPD to get profile-specific timestamps
      const auto& dashboard_gpd = profile->dashboard_gpd();
      if (dashboard_gpd.IsValid()) {
        auto profile_titles_info = dashboard_gpd.GetTitlesInfo();

        for (auto& game_entry : game_entries_) {
          // Update timestamp to this profile's last played time
          bool found = false;
          for (const auto& title_info : profile_titles_info) {
            if (title_info->title_id == game_entry.title_id) {
              if (title_info->last_played.is_valid()) {
                auto last_played_tp = chrono::WinSystemClock::to_sys(
                    title_info->last_played.to_time_point());
                game_entry.last_run_time =
                    std::chrono::system_clock::to_time_t(last_played_tp);
              } else {
                game_entry.last_run_time =
                    0;  // Profile hasn't played this game
              }
              found = true;
              break;
            }
          }
          // If not found in this profile's dashboard, the profile hasn't played
          // it
          if (!found) {
            game_entry.last_run_time = 0;
          }
        }
      }

      for (auto& game_entry : game_entries_) {
        // Get achievement stats from the profile
        auto stats = profile->GetTitleAchievementStats(game_entry.title_id);
        game_entry.achievements_total = stats.achievements_total;
        game_entry.achievements_unlocked = stats.achievements_unlocked;
        game_entry.gamerscore_total = stats.gamerscore_total;
        game_entry.gamerscore_earned = stats.gamerscore_earned;

        if (stats.achievements_total > 0) {
          XELOGI("Title {:08X}: {}/{} achievements, {}/{} gamerscore",
                 game_entry.title_id, game_entry.achievements_unlocked,
                 game_entry.achievements_total, game_entry.gamerscore_earned,
                 game_entry.gamerscore_total);
        }

        // Skip if we already loaded this icon
        if (title_icons_.find(game_entry.title_id) != title_icons_.end()) {
          continue;
        }

        // Get the title icon from the profile's title GPD
        auto icon_data = profile->GetTitleIcon(game_entry.title_id);
        if (icon_data.empty()) {
          XELOGI("No icon data for title {:08X} from profile {:016X}",
                 game_entry.title_id, profile->xuid());
          continue;  // No icon available for this title
        }

        // Create QPixmap from icon data
        QPixmap pixmap = CreateIconPixmap(icon_data);
        if (!pixmap.isNull()) {
          XELOGI("Loaded icon for title {:08X}, size: {} bytes",
                 game_entry.title_id, icon_data.size());
          title_icons_[game_entry.title_id] = pixmap;
        } else {
          XELOGI("Failed to create QPixmap for title {:08X}",
                 game_entry.title_id);
        }
      }
    }
  }
}

QPixmap GameListDialogQt::CreateIconPixmap(
    const std::vector<uint8_t>& icon_data) {
  if (icon_data.empty()) {
    return QPixmap();
  }

  QImage image;
  if (image.loadFromData(icon_data.data(),
                         static_cast<int>(icon_data.size()))) {
    return QPixmap::fromImage(image);
  }

  return QPixmap();
}

void GameListDialogQt::RefreshIcons() {
  // Clear existing icons and reload the entire game list from GPD
  title_icons_.clear();
  LoadGameList();
}

void GameListDialogQt::PopulateTable() {
  table_widget_->setRowCount(0);

  QString filter_text = search_box_ ? search_box_->text().toLower() : "";

  for (size_t i = 0; i < game_entries_.size(); i++) {
    const auto& entry = game_entries_[i];

    // Apply filter
    if (!filter_text.isEmpty()) {
      QString title = QString::fromStdString(entry.title_name).toLower();
      QString path =
          QString::fromStdString(entry.path_to_file.string()).toLower();

      if (!title.contains(filter_text) && !path.contains(filter_text)) {
        continue;
      }
    }

    int row = table_widget_->rowCount();
    table_widget_->insertRow(row);

    // Create a single container widget for the entire row
    auto* row_widget = new QWidget();
    row_widget->setAttribute(Qt::WA_Hover);
    row_widget->setObjectName("GameListRow");
    row_widget->setStyleSheet(
        "#GameListRow { border-bottom: 1px solid rgba(128, 128, 128, 50); }"
        "#GameListRow:hover { background-color: rgba(16, 124, 16, 80); }");
    auto* row_layout = new QHBoxLayout(row_widget);
    row_layout->setContentsMargins(10, 10, 10, 10);
    row_layout->setSpacing(15);

    // Icon
    auto* icon_label = new QLabel();
    icon_label->setAlignment(Qt::AlignCenter);
    icon_label->setMinimumSize(80, 80);
    icon_label->setMaximumSize(80, 80);
    icon_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    icon_label->setStyleSheet(
        "QLabel { border: 2px solid rgba(255, 255, 255, 100); border-radius: "
        "4px; }");

    auto icon_it = title_icons_.find(entry.title_id);
    if (icon_it != title_icons_.end() && !icon_it->second.isNull()) {
      icon_label->setPixmap(icon_it->second.scaled(80, 80, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation));
    } else {
      if (!has_logged_in_profile_) {
        icon_label->setText("Not\nlogged\nin");
        icon_label->setStyleSheet(
            "QLabel { color: gray; border: 2px solid rgba(255, 255, 255, 100); "
            "border-radius: 4px; }");
      } else {
        // Logged in but no icon - user hasn't played this game
        icon_label->setText("Not\nplayed");
        icon_label->setStyleSheet(
            "QLabel { color: gray; border: 2px solid rgba(255, 255, 255, 100); "
            "border-radius: 4px; }");
      }
    }
    row_layout->addWidget(icon_label);

    // Title container with title and path
    auto* title_container = new QWidget();
    title_container->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* title_layout = new QVBoxLayout(title_container);
    title_layout->setContentsMargins(0, 0, 0, 0);
    title_layout->setSpacing(2);

    // Check if file is corrupted (GPD has no title name)
    bool file_corrupted = entry.title_name.empty();

    // Title - large font
    QString display_title = file_corrupted
                                ? QString("File Corrupted")
                                : QString::fromStdString(entry.title_name);
    auto* title_label = new QLabel(display_title);
    title_label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    title_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont title_font = title_label->font();
    title_font.setBold(true);
    title_font.setPointSize(title_font.pointSize() * 1.5);  // 1.5x larger
    title_label->setFont(title_font);
    if (file_corrupted) {
      title_label->setStyleSheet("color: red;");
    }
    title_layout->addWidget(title_label);

    // Path - smaller font (only show if path is available)
    if (!entry.path_to_file.empty()) {
      auto* path_label =
          new QLabel(QString::fromStdString(entry.path_to_file.string()));
      path_label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
      path_label->setAttribute(Qt::WA_TransparentForMouseEvents);
      QFont path_font = path_label->font();
      path_font.setPointSize(path_font.pointSize() * 0.8);  // Smaller font
      path_label->setFont(path_font);
      path_label->setStyleSheet("color: gray;");
      title_layout->addWidget(path_label);
    }

    // Achievement info - smaller font (only show if there are achievements)
    if (entry.achievements_total > 0) {
      QString achievement_text =
          QString("Achievements: %1/%2 | Gamerscore: %3/%4")
              .arg(entry.achievements_unlocked)
              .arg(entry.achievements_total)
              .arg(entry.gamerscore_earned)
              .arg(entry.gamerscore_total);
      auto* achievement_label = new QLabel(achievement_text);
      achievement_label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
      achievement_label->setAttribute(Qt::WA_TransparentForMouseEvents);
      QFont achievement_font = achievement_label->font();
      achievement_font.setPointSize(achievement_font.pointSize() *
                                    0.8);  // Smaller font
      achievement_label->setFont(achievement_font);

      // Color based on completion
      if (entry.achievements_unlocked == entry.achievements_total &&
          entry.achievements_total > 0) {
        achievement_label->setStyleSheet("color: #4CAF50;");  // Green for 100%
      } else if (entry.achievements_unlocked > 0) {
        achievement_label->setStyleSheet(
            "color: #FFA726;");  // Orange for partial
      } else {
        achievement_label->setStyleSheet("color: gray;");  // Gray for none
      }
      title_layout->addWidget(achievement_label);
    }

    row_layout->addWidget(title_container, 1);  // Stretch factor

    // Last played
    auto* last_played_label = new QLabel(
        QString::fromStdString(FormatLastPlayed(entry.last_run_time)));
    last_played_label->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    last_played_label->setMinimumWidth(200);
    last_played_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    row_layout->addWidget(last_played_label);

    table_widget_->setCellWidget(row, 0, row_widget);
    table_widget_->setRowHeight(row, 100);

    // Store the path and title_id in the row for later retrieval
    table_widget_->setItem(row, 0, new QTableWidgetItem());
    table_widget_->item(row, 0)->setData(
        Qt::UserRole, QString::fromStdString(entry.path_to_file.string()));
    table_widget_->item(row, 0)->setData(Qt::UserRole + 1, entry.title_id);
  }
}

std::string GameListDialogQt::FormatLastPlayed(time_t timestamp) {
  if (timestamp == 0) {
    return "-";
  }

  return fmt::format("{:%Y-%m-%d %H:%M}", fmt::localtime(timestamp));
}

void GameListDialogQt::OnFilterTextChanged(const QString& text) {
  PopulateTable();
}

void GameListDialogQt::OnGameDoubleClicked(int row, int column) {
  auto* item = table_widget_->item(row, 0);
  if (!item) {
    return;
  }

  QString path_str = item->data(Qt::UserRole).toString();
  uint32_t title_id = item->data(Qt::UserRole + 1).toUInt();

  if (path_str.isEmpty()) {
    // No path available - show message and open file picker
    LaunchGameWithFilePicker();
    return;
  }

  std::filesystem::path path = path_str.toStdString();
  LaunchGame(path, title_id);
}

void GameListDialogQt::OnGameRightClicked(const QPoint& pos) {
  int row = table_widget_->rowAt(pos.y());
  if (row < 0) {
    return;
  }

  auto* item = table_widget_->item(row, 0);
  if (!item) {
    return;
  }

  QString path_str = item->data(Qt::UserRole).toString();
  uint32_t title_id = item->data(Qt::UserRole + 1).toUInt();

  std::filesystem::path path = path_str.toStdString();
  bool has_path = !path_str.isEmpty();

  // Get profile manager
  auto kernel_state = emulator_window_->emulator()->kernel_state();
  auto xam_state = kernel_state->xam_state();
  auto profile_manager = xam_state->profile_manager();

  QMenu context_menu;

  // Always show launch/open option, but change text based on path availability
  QAction* launch_action = nullptr;
  QAction* open_folder_action = nullptr;

  if (has_path) {
    launch_action = context_menu.addAction("Launch");
    open_folder_action = context_menu.addAction("Open containing folder");
  } else {
    launch_action = context_menu.addAction("Open");
  }

  // Achievements option (enabled if user is logged in)
  QAction* achievements_action = nullptr;
  bool is_signedin = false;
  uint64_t xuid = 0;

  // TODO: Handle multiple signed-in profiles better
  // For now, just use the first logged-in profile we find
  for (uint8_t user_index = 0; user_index < XUserMaxUserCount; user_index++) {
    const auto profile = profile_manager->GetProfile(user_index);
    if (profile) {
      is_signedin = true;
      xuid = profile->xuid();
      break;
    }
  }

  if (is_signedin && title_id != 0) {
    achievements_action = context_menu.addAction("Achievements");
    connect(achievements_action, &QAction::triggered, [=, this]() {
      // Get the title name from the game entry
      QString title_name;
      for (const auto& entry : game_entries_) {
        if (entry.title_id == title_id) {
          title_name = QString::fromStdString(entry.title_name);
          break;
        }
      }
      ShowAchievementsDialog(xuid, title_id, title_name);
    });
  }

  // Separator before Remove option
  context_menu.addSeparator();

  // Remove option is always available for all entries (at the bottom, in red)
  QAction* remove_action = context_menu.addAction("🗑 Remove from list");

  QAction* selected = context_menu.exec(table_widget_->mapToGlobal(pos));

  if (selected == launch_action && launch_action) {
    if (has_path) {
      LaunchGame(path, title_id);
    } else {
      LaunchGameWithFilePicker();
    }
  } else if (selected == open_folder_action && open_folder_action) {
    OpenContainingFolder(path);
  } else if (selected == achievements_action && achievements_action) {
    // Achievements dialog will open in a separate call
  } else if (selected == remove_action) {
    RemoveTitleFromDashboard(title_id);
  }
}

void GameListDialogQt::LaunchGame(const std::filesystem::path& path,
                                  uint32_t title_id) {
  if (emulator_window_) {
    if (emulator_window_->HasRunningChildProcess()) {
      return;
    }
    emulator_window_->LaunchTitleInNewProcess(path, false, title_id);
    // Widget stays visible at all times
  }
}

void GameListDialogQt::LaunchGameWithFilePicker() {
  // Show message to user
  QMessageBox::information(this, "Game Path Not Found",
                           "The installation path for this game is not known.\n"
                           "Please select the game file to launch it.");

  // Open file picker (same as File/Open)
  if (emulator_window_) {
    emulator_window_->FileOpen();
  }
}

void GameListDialogQt::OpenContainingFolder(const std::filesystem::path& path) {
  std::filesystem::path folder = path.parent_path();
  std::thread path_open(LaunchFileExplorer, folder);
  path_open.detach();
}

void GameListDialogQt::RemoveTitleFromDashboard(uint32_t title_id) {
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

  // Ask for confirmation
  QMessageBox::StandardButton reply;
  reply = QMessageBox::question(
      nullptr, "Remove Title",
      QString("Are you sure you want to remove this title from the game list?"),
      QMessageBox::Yes | QMessageBox::No);

  if (reply != QMessageBox::Yes) {
    return;
  }

  // Remove the title from all profiles' dashboard GPDs
  auto content_root = emulator_window_->emulator()->content_root();
  auto profiles_directory = xe::filesystem::FilterByName(
      xe::filesystem::ListDirectories(content_root),
      std::regex("[0-9A-F]{16}"));

  bool removed_from_any = false;

  for (const auto& profile_dir : profiles_directory) {
    const std::string profile_xuid = xe::path_to_utf8(profile_dir.name);
    if (profile_xuid == fmt::format("{:016X}", 0)) {
      continue;  // Skip shared content directory
    }

    // Construct path to dashboard GPD
    std::filesystem::path dashboard_gpd_path =
        profile_dir.path / profile_dir.name / kernel::xam::kDashboardStringID /
        fmt::format("{:08X}", static_cast<uint32_t>(XContentType::kProfile)) /
        profile_dir.name / fmt::format("{:08X}.gpd", kernel::kDashboardID);

    if (!std::filesystem::exists(dashboard_gpd_path)) {
      continue;
    }

    // Read dashboard GPD file
    std::ifstream file(dashboard_gpd_path, std::ios::binary);
    if (!file.is_open()) {
      continue;
    }

    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> gpd_data(file_size);
    file.read(reinterpret_cast<char*>(gpd_data.data()), file_size);
    file.close();

    // Parse dashboard GPD
    kernel::xam::GpdInfoProfile dashboard_gpd(gpd_data);
    if (!dashboard_gpd.IsValid()) {
      continue;
    }

    // Try to remove the title
    if (dashboard_gpd.RemoveTitle(title_id)) {
      // Write back the modified GPD
      std::vector<uint8_t> serialized_gpd = dashboard_gpd.Serialize();

      std::ofstream out_file(dashboard_gpd_path, std::ios::binary);
      if (out_file.is_open()) {
        out_file.write(reinterpret_cast<const char*>(serialized_gpd.data()),
                       serialized_gpd.size());
        out_file.close();
        removed_from_any = true;
      }
    }
  }

  if (removed_from_any) {
    // Reload the game list
    LoadGameList();
  } else {
    QMessageBox::warning(nullptr, "Remove Title",
                         "Failed to remove title from dashboard.");
  }
}

void GameListDialogQt::OnPlayClicked() {
  if (!selected_game_path_.empty()) {
    LaunchGame(selected_game_path_, selected_game_title_id_);
  } else {
    // No path available for selected game - show message and open file picker
    LaunchGameWithFilePicker();
  }
}

void GameListDialogQt::OnSettingsClicked() {
  if (emulator_window_) {
    emulator_window_->ToggleConfigDialog();
  }
}

void GameListDialogQt::OnProfileClicked() {
  // If logged in, show context menu instead
  if (current_profile_xuid_ != 0) {
    // Show context menu at the cursor position
    QPoint global_pos = QCursor::pos();
    QPoint local_pos = profile_button_->mapFromGlobal(global_pos);
    OnProfileContextMenu(local_pos);
    return;
  }

  // Not logged in, open profile dialog via emulator window
  if (emulator_window_) {
    emulator_window_->ToggleProfilesConfigDialog();
  }
}

void GameListDialogQt::OnSelectionChanged() {
  auto selected_items = table_widget_->selectedItems();
  if (!selected_items.isEmpty()) {
    int row = selected_items[0]->row();
    auto* item = table_widget_->item(row, 0);
    if (item) {
      QString path_str = item->data(Qt::UserRole).toString();
      uint32_t title_id = item->data(Qt::UserRole + 1).toUInt();
      if (!path_str.isEmpty()) {
        selected_game_path_ = path_str.toStdString();
        selected_game_title_id_ = title_id;
        UpdatePlayButtonState();
        return;
      }
    }
  }

  // No selection
  selected_game_path_.clear();
  selected_game_title_id_ = 0;
  UpdatePlayButtonState();
}

void GameListDialogQt::UpdatePlayButtonState() {
  bool game_is_running =
      emulator_window_ && emulator_window_->HasRunningChildProcess();

  // Update button appearance based on game state
  if (game_is_running) {
    play_button_->setIcon(QIcon(":/xenia/pause-icon.png"));
    play_label_->setText("Pause");
    play_button_->setToolTip("Game is running");
    play_button_->setEnabled(false);
    // Greyed out when game is running
    play_opacity_->setOpacity(0.4);
    play_label_opacity_->setOpacity(0.4);
  } else {
    play_button_->setIcon(QIcon(":/xenia/play-icon.png"));
    play_label_->setText("Play");
    play_button_->setToolTip("Launch selected game");
    bool is_enabled = !selected_game_path_.empty();
    play_button_->setEnabled(is_enabled);

    // Greyed out when disabled, normal when enabled
    if (is_enabled) {
      play_opacity_->setOpacity(1.0);  // Normal appearance
      play_label_opacity_->setOpacity(1.0);
    } else {
      play_opacity_->setOpacity(0.4);  // Greyed out
      play_label_opacity_->setOpacity(0.4);
    }
  }

  last_game_running_state_ = game_is_running;
}

void GameListDialogQt::UpdateProfileButtonState() {
  if (!emulator_window_ || !emulator_window_->emulator()) {
    profile_label_->setText("Logged Out");
    profile_question_label_->setVisible(true);
    profile_button_->setIcon(QIcon(":/xenia/user-icon.png"));
    return;
  }

  auto kernel_state = emulator_window_->emulator()->kernel_state();
  if (!kernel_state) {
    profile_label_->setText("Logged Out");
    profile_question_label_->setVisible(true);
    profile_button_->setIcon(QIcon(":/xenia/user-icon.png"));
    return;
  }

  auto xam_state = kernel_state->xam_state();
  if (!xam_state) {
    profile_label_->setText("Logged Out");
    profile_question_label_->setVisible(true);
    profile_button_->setIcon(QIcon(":/xenia/user-icon.png"));
    return;
  }

  auto profile_manager = xam_state->profile_manager();
  if (!profile_manager) {
    profile_label_->setText("Logged Out");
    profile_question_label_->setVisible(true);
    profile_button_->setIcon(QIcon(":/xenia/user-icon.png"));
    return;
  }

  // Check if any profile is logged in
  for (uint8_t user_index = 0; user_index < XUserMaxUserCount; user_index++) {
    const auto profile = profile_manager->GetProfile(user_index);
    if (profile) {
      // Found a logged in profile, show gamertag
      auto accounts = profile_manager->GetAccounts();
      auto account_it = accounts->find(profile->xuid());
      if (account_it != accounts->end()) {
        QString gamertag =
            QString::fromStdString(account_it->second.GetGamertagString());
        profile_label_->setText(gamertag);
        profile_question_label_->setVisible(
            false);  // Hide question mark when logged in
        current_profile_xuid_ = profile->xuid();

        // Load and display profile picture
        const auto profile_icon =
            profile->GetProfileIcon(kernel::xam::XTileType::kPersonalGamerTile);
        if (!profile_icon.empty()) {
          // Use personal gamer tile if available
          QPixmap pixmap;
          if (pixmap.loadFromData(profile_icon.data(),
                                  static_cast<int>(profile_icon.size()))) {
            profile_button_->setIcon(QIcon(pixmap));
            return;
          }
        }

        // Fall back to regular gamer tile
        const auto gamer_tile =
            profile->GetProfileIcon(kernel::xam::XTileType::kGamerTile);
        if (!gamer_tile.empty()) {
          QPixmap pixmap;
          if (pixmap.loadFromData(gamer_tile.data(),
                                  static_cast<int>(gamer_tile.size()))) {
            profile_button_->setIcon(QIcon(pixmap));
            return;
          }
        }

        // If no profile icon is available, keep the default icon
        profile_button_->setIcon(QIcon(":/xenia/user-icon.png"));
        return;
      }
    }
  }

  // No profiles logged in
  profile_label_->setText("Logged Out");
  profile_question_label_->setVisible(true);
  profile_button_->setIcon(QIcon(":/xenia/user-icon.png"));
  current_profile_xuid_ = 0;
}

void GameListDialogQt::OnProfileContextMenu(const QPoint& pos) {
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

  // If there's a logged in profile, show the context menu for it
  if (current_profile_xuid_ == 0) {
    // No profile logged in, open the profile dialog
    OnProfileClicked();
    return;
  }

  uint64_t xuid = current_profile_xuid_;
  const uint8_t user_index =
      profile_manager->GetUserIndexAssignedToProfile(xuid);

  QMenu context_menu;

  if (user_index != XUserIndexAny) {
    QAction* logout_action = context_menu.addAction("Logout");
    connect(logout_action, &QAction::triggered, [=, this]() {
      profile_manager->Logout(user_index);
      UpdateProfileButtonState();
      RefreshIcons();  // Refresh icons after logout
    });
  }

  // Modify (Gamercard)
  QAction* modify_action = context_menu.addAction("Modify");
  connect(modify_action, &QAction::triggered, [=, this]() {
    auto* editor_dialog =
        new ProfileEditorDialogQt(nullptr, emulator_window_, xuid);
    editor_dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(editor_dialog, &QDialog::finished, [this](int result) {
      if (result == QDialog::Accepted) {
        UpdateProfileButtonState();
        RefreshIcons();
      }
    });
    editor_dialog->show();
    editor_dialog->raise();
    editor_dialog->activateWindow();
  });

  QAction* show_content_action =
      context_menu.addAction("Show Content Directory");
  connect(show_content_action, &QAction::triggered, [=, this]() {
    const auto path = profile_manager->GetProfileContentPath(
        xuid, emulator_window_->emulator()->kernel_state()->title_id());

    if (!std::filesystem::exists(path)) {
      std::filesystem::create_directories(path);
    }

    std::thread path_open(LaunchFileExplorer, path);
    path_open.detach();
  });

  context_menu.addSeparator();

  // Profiles Menu
  QAction* profiles_menu_action = context_menu.addAction("Profiles Menu");
  connect(profiles_menu_action, &QAction::triggered, [this]() {
    if (emulator_window_) {
      emulator_window_->ToggleProfilesConfigDialog();
    }
  });

  context_menu.exec(profile_button_->mapToGlobal(pos));
}

bool GameListDialogQt::eventFilter(QObject* obj, QEvent* event) {
  if (obj == table_widget_->viewport() && event->type() == QEvent::Wheel) {
    ShowScrollbar();
    return false;  // Let the event propagate
  }
  return QWidget::eventFilter(obj, event);
}

void GameListDialogQt::ShowScrollbar() {
  if (!table_widget_) {
    return;
  }

  // Show scrollbar by making handle visible
  table_widget_->verticalScrollBar()->setStyleSheet(
      "QScrollBar:vertical {"
      "    border: none;"
      "    background: transparent;"
      "    width: 10px;"
      "    margin: 0px;"
      "}"
      "QScrollBar::handle:vertical {"
      "    background: rgba(16, 124, 16, 180);"
      "    min-height: 20px;"
      "    border-radius: 5px;"
      "}"
      "QScrollBar::handle:vertical:hover {"
      "    background: rgba(16, 124, 16, 220);"
      "}"
      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
      "    height: 0px;"
      "}"
      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
      "    background: none;"
      "}");

  // Restart hide timer
  if (scrollbar_hide_timer_) {
    scrollbar_hide_timer_->start();
  }
}

void GameListDialogQt::HideScrollbar() {
  if (!table_widget_) {
    return;
  }

  // Hide scrollbar by making handle transparent
  table_widget_->verticalScrollBar()->setStyleSheet(
      "QScrollBar:vertical {"
      "    border: none;"
      "    background: transparent;"
      "    width: 10px;"
      "    margin: 0px;"
      "}"
      "QScrollBar::handle:vertical {"
      "    background: rgba(16, 124, 16, 0);"
      "    min-height: 20px;"
      "    border-radius: 5px;"
      "}"
      "QScrollBar::handle:vertical:hover {"
      "    background: rgba(16, 124, 16, 180);"
      "}"
      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
      "    height: 0px;"
      "}"
      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
      "    background: none;"
      "}");
}

void GameListDialogQt::ShowAchievementsDialog(uint64_t xuid, uint32_t title_id,
                                              const QString& title_name) {
  if (!emulator_window_ || !emulator_window_->emulator()) {
    return;
  }

  auto kernel_state = emulator_window_->emulator()->kernel_state();
  if (!kernel_state) {
    return;
  }

  auto user_tracker = kernel_state->xam_state()->user_tracker();
  const auto title_info = user_tracker->GetUserTitleInfo(xuid, title_id);
  if (!title_info) {
    return;
  }

  const auto profile = kernel_state->xam_state()->GetUserProfile(xuid);
  if (!profile) {
    return;
  }

  auto* achievements_dialog = new xe::ui::AchievementsDialogQt(
      nullptr, kernel_state, &title_info.value(), profile);
  achievements_dialog->setAttribute(Qt::WA_DeleteOnClose);
  achievements_dialog->show();
  achievements_dialog->raise();
  achievements_dialog->activateWindow();
}

}  // namespace app
}  // namespace xe
