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
#include <QHeaderView>
#include <QImage>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QUrl>
#include <chrono>
#include <thread>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/app/emulator_window.h"
#include "xenia/app/profile_dialog_qt.h"
#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"
#include "xenia/base/system.h"
#include "xenia/base/utf8.h"
#include "xenia/emulator.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/profile_manager.h"
#include "xenia/kernel/xam/ui/gamercard_ui.h"
#include "xenia/kernel/xam/ui/title_info_ui.h"
#include "xenia/kernel/xam/user_tracker.h"
#include "xenia/kernel/xam/xam_state.h"

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
  open_button->setText("📂");
  open_button->setMinimumSize(100, 80);
  open_button->setMaximumSize(100, 80);
  QFont open_icon_font = open_button->font();
  open_icon_font.setPointSize(48);
  open_button->setFont(open_icon_font);
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

  // Use a button with an icon label overlay for better centering
  play_button_ = new QToolButton(this);
  play_button_->setMinimumSize(100, 80);
  play_button_->setMaximumSize(100, 80);
  play_button_->setEnabled(false);
  connect(play_button_, &QToolButton::clicked, this,
          &GameListDialogQt::OnPlayClicked);

  play_icon_label_ = new QLabel("▶", play_button_);
  play_icon_label_->setAlignment(Qt::AlignCenter);
  QFont icon_font = play_icon_label_->font();
  icon_font.setPointSize(48);
  play_icon_label_->setFont(icon_font);
  play_icon_label_->setGeometry(0, -8, 100, 80);  // Shift up by 8px
  play_icon_label_->setAttribute(Qt::WA_TransparentForMouseEvents);

  play_label_ = new QLabel("Play", this);
  play_label_->setAlignment(Qt::AlignCenter);
  QFont text_font = play_label_->font();
  text_font.setPointSize(10);
  play_label_->setFont(text_font);
  open_label->setFont(text_font);  // Apply to open label too
  play_label_->setFixedHeight(20);

  play_layout->addWidget(play_button_);
  play_layout->addWidget(play_label_);
  toolbar_layout->addWidget(play_container);

  // Settings button
  auto* settings_container = new QWidget(this);
  auto* settings_layout = new QVBoxLayout(settings_container);
  settings_layout->setContentsMargins(0, 0, 0, 0);
  settings_layout->setSpacing(5);

  settings_button_ = new QToolButton(this);
  settings_button_->setText("⚙");
  settings_button_->setMinimumSize(100, 80);
  settings_button_->setMaximumSize(100, 80);
  settings_button_->setFont(icon_font);
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
  profile_button_->setText("👤");
  profile_button_->setMinimumSize(100, 80);
  profile_button_->setMaximumSize(100, 80);
  profile_button_->setFont(icon_font);
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
  connect(search_box_, &QLineEdit::textChanged, this,
          &GameListDialogQt::OnFilterTextChanged);

  // Only show search box if there are more than 5 games
  search_box_->setVisible(false);

  main_layout->addWidget(search_box_);

  // Table widget
  table_widget_ = new QTableWidget(this);
  table_widget_->setColumnCount(3);
  table_widget_->setHorizontalHeaderLabels({"Icon", "Title", "Last Played"});
  table_widget_->horizontalHeader()->setStretchLastSection(false);
  table_widget_->horizontalHeader()->setSectionResizeMode(1,
                                                          QHeaderView::Stretch);
  table_widget_->verticalHeader()->setVisible(false);
  table_widget_->setShowGrid(false);
  table_widget_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_widget_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_widget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_widget_->setContextMenuPolicy(Qt::CustomContextMenu);

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

  if (!emulator_window_) {
    return;
  }

  const auto& emulator_recent_titles =
      emulator_window_->GetRecentlyLaunchedTitles();

  for (const auto& entry : emulator_recent_titles) {
    game_entries_.push_back(
        {entry.title_name, entry.path_to_file, entry.last_run_time, 0, {}});
  }

  // Show/hide search box based on number of entries
  if (search_box_) {
    search_box_->setVisible(game_entries_.size() > 5);
  }

  TryLoadIcons();
  PopulateTable();
}

void GameListDialogQt::TryLoadIcons() {
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

  auto user_tracker = xam_state->user_tracker();
  auto profile_manager = xam_state->profile_manager();

  if (!user_tracker || !profile_manager) {
    return;
  }

  // Check if the number of logged-in profiles has changed
  has_logged_in_profile_ = false;
  int current_logged_in_count = 0;

  for (uint8_t i = 0; i < 4; i++) {
    if (profile_manager->GetProfile(i)) {
      current_logged_in_count++;
      has_logged_in_profile_ = true;
    }
  }

  // If the count changed, refresh icons
  if (current_logged_in_count != last_logged_in_count_) {
    XELOGI("Logged-in profile count changed from {} to {}, refreshing icons",
           last_logged_in_count_, current_logged_in_count);
    last_logged_in_count_ = current_logged_in_count;
    RefreshIcons();
  }

  // Check all logged in profiles
  for (uint8_t user_index = 0; user_index < 4; user_index++) {
    const auto profile = profile_manager->GetProfile(user_index);
    if (!profile) {
      continue;
    }

    // Get all played titles for this profile
    auto played_titles = user_tracker->GetPlayedTitles(profile->xuid());

    // Match each game entry with played titles by name
    for (auto& game_entry : game_entries_) {
      for (const auto& played_title : played_titles) {
        std::string played_name = xe::to_utf8(played_title.title_name);
        // Remove null terminator if present
        if (!played_name.empty() && played_name.back() == '\0') {
          played_name.pop_back();
        }
        std::string trimmed_played = xe::string_util::trim(played_name);
        std::string trimmed_game = xe::string_util::trim(game_entry.title_name);

        if (trimmed_played == trimmed_game) {
          if (!played_title.icon.empty()) {
            game_entry.icon = std::vector<uint8_t>(played_title.icon.begin(),
                                                   played_title.icon.end());
            game_entry.title_id = played_title.id;

            // Create QPixmap from icon data
            QPixmap pixmap = CreateIconPixmap(game_entry.icon);
            if (!pixmap.isNull()) {
              title_icons_[game_entry.title_id] = pixmap;
            }
          }
          break;  // Found match for this game
        }
      }
    }
  }

  // Refresh the table to show the icons
  PopulateTable();
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
  // Clear existing icons
  title_icons_.clear();

  // Reset the title IDs to force re-matching
  for (auto& entry : game_entries_) {
    entry.title_id = 0;
    entry.icon.clear();
  }

  // Reload icons
  TryLoadIcons();
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

    // Icon column
    auto* icon_label = new QLabel();
    icon_label->setAlignment(Qt::AlignCenter);
    icon_label->setMinimumSize(80, 80);

    auto icon_it = title_icons_.find(entry.title_id);
    if (icon_it != title_icons_.end() && !icon_it->second.isNull()) {
      icon_label->setPixmap(icon_it->second.scaled(80, 80, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation));
    } else {
      if (!has_logged_in_profile_) {
        icon_label->setText("Not\nlogged\nin");
        icon_label->setStyleSheet("color: gray;");
      } else {
        // Empty space if logged in but no icon
        icon_label->setText("");
      }
    }

    table_widget_->setCellWidget(row, 0, icon_label);

    // Title column - just the title, large font
    auto* title_label = new QLabel(QString::fromStdString(entry.title_name));
    title_label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    QFont title_font = title_label->font();
    title_font.setBold(true);
    title_font.setPointSize(title_font.pointSize() * 1.5);  // 1.5x larger
    title_label->setFont(title_font);
    title_label->setContentsMargins(10, 0, 10, 0);
    table_widget_->setCellWidget(row, 1, title_label);

    // Last played column
    auto* last_played_label = new QLabel(
        QString::fromStdString(FormatLastPlayed(entry.last_run_time)));
    last_played_label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    last_played_label->setContentsMargins(10, 0, 10, 0);
    table_widget_->setCellWidget(row, 2, last_played_label);

    table_widget_->setRowHeight(row, 100);

    // Store the path in the row for later retrieval
    table_widget_->setItem(row, 0, new QTableWidgetItem());
    table_widget_->item(row, 0)->setData(
        Qt::UserRole, QString::fromStdString(entry.path_to_file.string()));
  }

  // Adjust column widths
  table_widget_->setColumnWidth(0, 100);
  table_widget_->setColumnWidth(2, 200);  // Fixed width for last played
}

std::string GameListDialogQt::FormatLastPlayed(time_t timestamp) {
  if (timestamp == 0) {
    return "Unknown";
  }

  return fmt::format("{:%Y-%m-%d %H:%M}", std::chrono::system_clock::time_point(
                                              std::chrono::seconds(timestamp)));
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
  if (path_str.isEmpty()) {
    return;
  }

  std::filesystem::path path = path_str.toStdString();
  LaunchGame(path);
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
  if (path_str.isEmpty()) {
    return;
  }

  std::filesystem::path path = path_str.toStdString();

  QMenu context_menu(this);
  QAction* launch_action = context_menu.addAction("Launch");
  QAction* open_folder_action =
      context_menu.addAction("Open containing folder");

  QAction* selected = context_menu.exec(table_widget_->mapToGlobal(pos));

  if (selected == launch_action) {
    LaunchGame(path);
  } else if (selected == open_folder_action) {
    OpenContainingFolder(path);
  }
}

void GameListDialogQt::LaunchGame(const std::filesystem::path& path) {
  if (emulator_window_) {
    if (emulator_window_->HasRunningChildProcess()) {
      return;
    }
    emulator_window_->LaunchTitleInNewProcess(path);
    // Widget stays visible at all times
  }
}

void GameListDialogQt::OpenContainingFolder(const std::filesystem::path& path) {
  std::filesystem::path folder = path.parent_path();
  std::thread path_open(LaunchFileExplorer, folder);
  path_open.detach();
}

void GameListDialogQt::OnPlayClicked() {
  if (!selected_game_path_.empty()) {
    LaunchGame(selected_game_path_);
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
      if (!path_str.isEmpty()) {
        selected_game_path_ = path_str.toStdString();
        UpdatePlayButtonState();
        return;
      }
    }
  }

  // No selection
  selected_game_path_.clear();
  play_button_->setEnabled(false);
}

void GameListDialogQt::UpdatePlayButtonState() {
  bool game_is_running =
      emulator_window_ && emulator_window_->HasRunningChildProcess();

  // Update button appearance based on game state
  if (game_is_running) {
    play_icon_label_->setText("⏸");
    play_label_->setText("Pause");
    play_button_->setToolTip("Game is running");
    play_button_->setEnabled(false);
  } else {
    play_icon_label_->setText("▶");
    play_label_->setText("Play");
    play_button_->setToolTip("Launch selected game");
    play_button_->setEnabled(!selected_game_path_.empty());
  }

  last_game_running_state_ = game_is_running;
}

void GameListDialogQt::UpdateProfileButtonState() {
  if (!emulator_window_ || !emulator_window_->emulator()) {
    profile_label_->setText("Logged Out");
    profile_question_label_->setVisible(true);
    return;
  }

  auto kernel_state = emulator_window_->emulator()->kernel_state();
  if (!kernel_state) {
    profile_label_->setText("Logged Out");
    profile_question_label_->setVisible(true);
    return;
  }

  auto xam_state = kernel_state->xam_state();
  if (!xam_state) {
    profile_label_->setText("Logged Out");
    profile_question_label_->setVisible(true);
    return;
  }

  auto profile_manager = xam_state->profile_manager();
  if (!profile_manager) {
    profile_label_->setText("Logged Out");
    profile_question_label_->setVisible(true);
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
        return;
      }
    }
  }

  // No profiles logged in
  profile_label_->setText("Logged Out");
  profile_question_label_->setVisible(true);
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

  QMenu context_menu(this);

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
    new kernel::xam::ui::GamercardUI(
        emulator_window_->window(), emulator_window_->imgui_drawer(),
        emulator_window_->emulator()->kernel_state(), xuid);
  });

  // Show Played Titles (disabled if not signed in)
  const bool is_signedin = profile_manager->GetProfile(xuid) != nullptr;
  QAction* played_titles_action = context_menu.addAction("Show Played Titles");
  played_titles_action->setEnabled(is_signedin);
  connect(played_titles_action, &QAction::triggered, [=, this]() {
    new kernel::xam::ui::TitleListUI(emulator_window_->imgui_drawer(),
                                     ImVec2(500, 100),
                                     profile_manager->GetProfile(user_index));
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
      "    background: rgba(128, 128, 128, 180);"
      "    min-height: 20px;"
      "    border-radius: 5px;"
      "}"
      "QScrollBar::handle:vertical:hover {"
      "    background: rgba(128, 128, 128, 220);"
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
      "    background: rgba(128, 128, 128, 0);"
      "    min-height: 20px;"
      "    border-radius: 5px;"
      "}"
      "QScrollBar::handle:vertical:hover {"
      "    background: rgba(128, 128, 128, 180);"
      "}"
      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
      "    height: 0px;"
      "}"
      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
      "    background: none;"
      "}");
}

}  // namespace app
}  // namespace xe
