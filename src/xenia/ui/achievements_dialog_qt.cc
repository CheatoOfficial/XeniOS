/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/achievements_dialog_qt.h"

#include <QApplication>
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/chrono.h"
#include "xenia/base/logging.h"
#include "xenia/base/utf8.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/achievement_manager.h"
#include "xenia/kernel/xam/profile_manager.h"
#include "xenia/kernel/xam/xam_state.h"
#include "xenia/ui/qt_util.h"

namespace xe {
namespace kernel {
namespace xam {
struct Achievement;
}
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace ui {

using xe::ui::SafeQString;

AchievementsDialogQt::AchievementsDialogQt(
    QWidget* parent, kernel::KernelState* kernel_state,
    const kernel::xam::TitleInfo* title_info,
    const kernel::xam::UserProfile* profile)
    : QDialog(parent),
      kernel_state_(kernel_state),
      title_info_(title_info),
      profile_(profile),
      xuid_(profile->xuid()),
      title_id_(title_info->id),
      title_name_(QString::fromStdU16String(title_info->title_name)),
      show_locked_info_(false) {
  setWindowTitle(title_name_.isEmpty() ? "Loading Achievements..."
                                       : title_name_ + " - Achievements");
  setModal(true);
  setAttribute(Qt::WA_DeleteOnClose);
  resize(800, 600);

  SetupUI();
  LoadAchievements();
}

AchievementsDialogQt::~AchievementsDialogQt() = default;

void AchievementsDialogQt::SetupUI() {
  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(10, 10, 10, 10);
  main_layout->setSpacing(10);

  // Title and summary section
  auto* header_layout = new QHBoxLayout();

  auto* title_label =
      new QLabel(title_name_.isEmpty() ? "Loading..." : title_name_, this);
  QFont title_font = title_label->font();
  title_font.setPointSize(title_font.pointSize() * 1.5);
  title_font.setBold(true);
  title_label->setFont(title_font);
  header_layout->addWidget(title_label);

  header_layout->addStretch();

  auto* refresh_button = new QPushButton("Refresh", this);
  connect(refresh_button, &QPushButton::clicked, this,
          &AchievementsDialogQt::OnRefreshClicked);
  header_layout->addWidget(refresh_button);

  main_layout->addLayout(header_layout);

  // Summary section
  summary_label_ = new QLabel("Loading achievements...", this);
  main_layout->addWidget(summary_label_);

  progress_bar_ = new QProgressBar(this);
  progress_bar_->setRange(0, 100);
  main_layout->addWidget(progress_bar_);

  // Show locked info checkbox
  show_locked_checkbox_ =
      new QCheckBox("Show locked achievements information", this);
  show_locked_checkbox_->setChecked(false);
  connect(show_locked_checkbox_, &QCheckBox::toggled, this,
          &AchievementsDialogQt::OnShowLockedInfoChanged);
  main_layout->addWidget(show_locked_checkbox_);

  // Separator
  auto* separator = new QFrame(this);
  separator->setFrameShape(QFrame::HLine);
  separator->setFrameShadow(QFrame::Sunken);
  main_layout->addWidget(separator);

  // Achievements table
  achievements_table_ = new QTableWidget(this);
  achievements_table_->setColumnCount(4);
  achievements_table_->setHorizontalHeaderLabels(
      {"Icon", "Achievement", "Gamer score", "Status"});

  // Configure table
  achievements_table_->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Fixed);
  achievements_table_->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::Stretch);
  achievements_table_->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::Fixed);
  achievements_table_->horizontalHeader()->setSectionResizeMode(
      3, QHeaderView::Fixed);
  achievements_table_->setColumnWidth(0, 64);
  achievements_table_->setColumnWidth(2, 110);
  achievements_table_->setColumnWidth(3, 130);

  achievements_table_->verticalHeader()->setVisible(false);
  achievements_table_->setShowGrid(false);
  achievements_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  achievements_table_->setSelectionMode(QAbstractItemView::SingleSelection);
  achievements_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  achievements_table_->setAlternatingRowColors(false);

  // Set consistent dark background and add separators
  achievements_table_->setStyleSheet(
      "QTableWidget {"
      "  background-color: #2D2D2D;"
      "  alternate-background-color: #2D2D2D;"
      "  gridline-color: #444444;"
      "  selection-background-color: rgba(16, 124, 16, 120);"
      "}"
      "QTableWidget::item {"
      "  border-bottom: 1px solid #444444;"
      "  padding: 0px;"
      "}"
      "QTableWidget::item:selected {"
      "  background-color: rgba(16, 124, 16, 120);"
      "}");

  // Row height - consistent minimum height with auto-resize for content
  achievements_table_->verticalHeader()->setSectionResizeMode(
      QHeaderView::ResizeToContents);
  achievements_table_->verticalHeader()->setMinimumSectionSize(64);

  main_layout->addWidget(achievements_table_);

  // Close button
  auto* button_layout = new QHBoxLayout();
  button_layout->addStretch();

  auto* close_button = new QPushButton("Close", this);
  connect(close_button, &QPushButton::clicked, this, &QDialog::accept);
  button_layout->addWidget(close_button);

  main_layout->addLayout(button_layout);
}

void AchievementsDialogQt::LoadAchievements() {
  if (!kernel_state_) {
    return;
  }

  auto xam_state = kernel_state_->xam_state();
  if (!xam_state) {
    return;
  }

  auto achievement_manager = xam_state->achievement_manager();
  if (!achievement_manager) {
    return;
  }

  // Load achievements
  achievements_ = achievement_manager->GetTitleAchievements(xuid_, title_id_);

  // Create a lock icon
  lock_icon_ = QPixmap(56, 56);
  lock_icon_.fill(Qt::transparent);
  QPainter painter(&lock_icon_);
  painter.setRenderHint(QPainter::Antialiasing);

  // Draw lock body
  painter.setPen(QPen(QColor(100, 100, 100), 2));
  painter.setBrush(QColor(150, 150, 150));
  QRectF lockRect(12, 20, 32, 28);
  painter.drawRoundedRect(lockRect, 4, 4);

  // Draw keyhole
  painter.setBrush(Qt::black);
  painter.setPen(Qt::NoPen);
  painter.drawEllipse(QPointF(28, 34), 3, 3);
  painter.drawRect(26, 34, 4, 8);

  // Draw lock shackle (centered)
  painter.setPen(QPen(QColor(100, 100, 100), 3));
  painter.setBrush(Qt::NoBrush);
  QPainterPath path;
  // Center the shackle over the lock body (lock body center is x=28)
  // Draw half-circle going UP from left to right
  path.moveTo(18, 20);                // Start from left side of lock body
  path.arcTo(18, 8, 20, 20, 0, 180);  // Half circle from 0° to 180° (arcs up)
  path.lineTo(38, 20);                // End at right side of lock body
  painter.drawPath(path);

  // Load achievement icons and scale them to 56x56
  for (const auto& achievement : achievements_) {
    auto icon_data = achievement_manager->GetAchievementIcon(
        xuid_, title_id_, achievement.achievement_id);

    if (!icon_data.empty()) {
      QPixmap pixmap;
      if (pixmap.loadFromData(icon_data.data(),
                              static_cast<int>(icon_data.size()))) {
        // Scale the icon to 56x56 to fit nicely in the 64x64 cell with some
        // padding
        achievement_icons_[achievement.achievement_id] = pixmap.scaled(
            56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      }
    }
  }

  PopulateAchievements();
  UpdateSummary();

  // Update window title with the actual title name
  setWindowTitle(title_name_.isEmpty() ? "Achievements"
                                       : title_name_ + " - Achievements");
}

void AchievementsDialogQt::PopulateAchievements() {
  achievements_table_->setRowCount(0);

  if (achievements_.empty()) {
    achievements_table_->setRowCount(1);
    auto* no_achievements_item =
        new QTableWidgetItem("No achievements data available");
    no_achievements_item->setFlags(Qt::NoItemFlags);
    achievements_table_->setItem(0, 0, no_achievements_item);
    achievements_table_->setSpan(0, 0, 1, 4);
    return;
  }

  // Sort achievements by unlock date (earliest first, locked last)
  auto sorted_achievements = achievements_;
  std::sort(sorted_achievements.begin(), sorted_achievements.end(),
            [](const xe::kernel::xam::Achievement& a,
               const xe::kernel::xam::Achievement& b) {
              // If both are unlocked, sort by unlock time
              if (a.IsUnlocked() && b.IsUnlocked()) {
                // Earlier unlock times come first
                return a.unlock_time.to_time_point() <
                       b.unlock_time.to_time_point();
              }
              // Unlocked achievements come before locked ones
              if (a.IsUnlocked() != b.IsUnlocked()) {
                return a.IsUnlocked();
              }
              // Both are locked, maintain original order
              return false;
            });

  achievements_table_->setRowCount(
      static_cast<int>(sorted_achievements.size()));

  for (int row = 0; row < static_cast<int>(sorted_achievements.size()); ++row) {
    const auto& achievement = sorted_achievements[row];

    // Icon column - use QLabel widget to properly display scaled icons
    auto* icon_label = new QLabel();
    QPixmap pixmap = GetAchievementIcon(achievement);
    if (!pixmap.isNull()) {
      icon_label->setPixmap(pixmap);
    }
    icon_label->setAlignment(Qt::AlignCenter);
    icon_label->setContentsMargins(4, 4, 4, 4);
    achievements_table_->setCellWidget(row, 0, icon_label);

    // Achievement title and description - use a widget for multi-line support
    auto* text_widget = new QWidget();
    auto* text_layout = new QVBoxLayout(text_widget);
    text_layout->setContentsMargins(4, 4, 4, 4);
    text_layout->setSpacing(1);

    auto* title_item = new QTableWidgetItem(GetAchievementTitle(achievement));
    QFont title_font = title_item->font();
    title_font.setBold(true);
    title_item->setFont(title_font);
    // We'll add this to the table later after checking if we need description

    // Title label
    auto* title_label = new QLabel(GetAchievementTitle(achievement));
    title_label->setFont(title_font);
    text_layout->addWidget(title_label);

    // Description label
    QString desc_text = GetAchievementDescription(achievement);
    if (!desc_text.isEmpty()) {
      auto* desc_label = new QLabel(desc_text);
      desc_label->setWordWrap(true);
      desc_label->setStyleSheet("color: gray; font-size: 11px;");
      text_layout->addWidget(desc_label);
    }

    achievements_table_->setCellWidget(row, 1, text_widget);

    // Gamerscore
    QString gamerscore_text = QString("%1 G").arg(achievement.gamerscore);
    auto* gamerscore_item = new QTableWidgetItem(gamerscore_text);
    gamerscore_item->setTextAlignment(Qt::AlignCenter);
    if (achievement.IsUnlocked()) {
      gamerscore_item->setForeground(QBrush(QColor("#4CAF50")));
    } else {
      gamerscore_item->setForeground(QBrush(QColor("gray")));
    }
    achievements_table_->setItem(row, 2, gamerscore_item);

    // Status - use a widget for multi-line support like achievement column
    auto* status_widget = new QWidget();
    auto* status_layout = new QVBoxLayout(status_widget);
    status_layout->setContentsMargins(4, 4, 4, 4);
    status_layout->setSpacing(1);
    status_layout->setAlignment(Qt::AlignCenter);

    auto* status_label =
        new QLabel(achievement.IsUnlocked() ? "Unlocked" : "Locked");
    status_label->setAlignment(Qt::AlignCenter);
    QFont status_font = status_label->font();
    status_font.setBold(true);
    status_label->setFont(status_font);
    if (achievement.IsUnlocked()) {
      status_label->setStyleSheet("color: #4CAF50;");
    } else {
      status_label->setStyleSheet("color: gray;");
    }
    status_layout->addWidget(status_label);

    // Date label for unlocked achievements
    if (achievement.IsUnlocked()) {
      QString unlock_time = GetUnlockedTime(achievement);
      auto* date_label = new QLabel(unlock_time);
      date_label->setAlignment(Qt::AlignCenter);
      date_label->setStyleSheet("color: gray; font-size: 11px;");
      status_layout->addWidget(date_label);
    }

    achievements_table_->setCellWidget(row, 3, status_widget);
  }

  setWindowTitle(QString("%1 Achievements").arg(title_name_));
}

QPixmap AchievementsDialogQt::GetAchievementIcon(
    const xe::kernel::xam::Achievement& achievement) {
  // If achievement is locked and user hasn't enabled "show locked info", show
  // lock
  if (!achievement.IsUnlocked() && !show_locked_info_) {
    return lock_icon_;
  }

  // Try to get the actual achievement icon
  auto it = achievement_icons_.find(achievement.achievement_id);
  if (it != achievement_icons_.end()) {
    // Show the actual icon (for unlocked, or locked with "show locked" enabled)
    return it->second;
  }

  // Icon unavailable - show lock as fallback
  return lock_icon_;
}

QString AchievementsDialogQt::GetAchievementTitle(
    const xe::kernel::xam::Achievement& achievement) {
  if (!achievement.IsUnlocked() && !show_locked_info_ &&
      !(achievement.flags & 0x1)) {  // kShowUnachieved
    return "Secret Achievement";
  }

  std::string title = xe::to_utf8(achievement.achievement_name);
  // Remove null terminator if present
  if (!title.empty() && title.back() == '\0') {
    title.pop_back();
  }
  return SafeQString(title);
}

QString AchievementsDialogQt::GetAchievementDescription(
    const xe::kernel::xam::Achievement& achievement) {
  if (!achievement.IsUnlocked()) {
    // For locked achievements
    if (show_locked_info_) {
      // Show locked description when checkbox is checked
      std::string desc = xe::to_utf8(achievement.locked_description);
      // Remove null terminator if present
      if (!desc.empty() && desc.back() == '\0') {
        desc.pop_back();
      }
      return SafeQString(desc);
    } else {
      // Hide description when checkbox is not checked
      return QString();
    }
  }

  // For unlocked achievements, always show the unlocked description
  std::string desc = xe::to_utf8(achievement.unlocked_description);
  // Remove null terminator if present
  if (!desc.empty() && desc.back() == '\0') {
    desc.pop_back();
  }
  return SafeQString(desc);
}

QString AchievementsDialogQt::GetUnlockedTime(
    const xe::kernel::xam::Achievement& achievement) {
  if (!achievement.IsUnlocked()) {
    return QString();
  }

  if (achievement.unlock_time.is_valid()) {
    auto unlock_tp =
        chrono::WinSystemClock::to_sys(achievement.unlock_time.to_time_point());
    auto unlock_time = std::chrono::system_clock::to_time_t(unlock_tp);

    return SafeQString(
        fmt::format("{:%Y-%m-%d %H:%M}", fmt::localtime(unlock_time)));
  }

  return "Unknown time";
}

void AchievementsDialogQt::UpdateSummary() {
  int unlocked = 0;
  int total = static_cast<int>(achievements_.size());
  int gamerscore_earned = 0;
  int gamerscore_total = 0;

  for (const auto& achievement : achievements_) {
    gamerscore_total += achievement.gamerscore;
    if (achievement.IsUnlocked()) {
      unlocked++;
      gamerscore_earned += achievement.gamerscore;
    }
  }

  double percentage =
      total > 0 ? (static_cast<double>(unlocked) / total * 100.0) : 0.0;

  summary_label_->setText(
      QString("%1 / %2 Achievements Unlocked (%3%) - %4 / %5 Gamerscore")
          .arg(unlocked)
          .arg(total)
          .arg(QString::number(percentage, 'f', 1))
          .arg(gamerscore_earned)
          .arg(gamerscore_total));

  progress_bar_->setValue(static_cast<int>(percentage));

  // Color code the progress bar
  if (percentage >= 100.0) {
    progress_bar_->setStyleSheet(
        "QProgressBar::chunk { background-color: #4CAF50; }");
  } else if (percentage >= 50.0) {
    progress_bar_->setStyleSheet(
        "QProgressBar::chunk { background-color: #FFA726; }");
  } else {
    progress_bar_->setStyleSheet(
        "QProgressBar::chunk { background-color: #666; }");
  }
}

void AchievementsDialogQt::OnShowLockedInfoChanged() {
  show_locked_info_ = show_locked_checkbox_->isChecked();
  PopulateAchievements();
}

void AchievementsDialogQt::OnRefreshClicked() {
  achievements_.clear();
  achievement_icons_.clear();
  LoadAchievements();
}

}  // namespace ui
}  // namespace xe
