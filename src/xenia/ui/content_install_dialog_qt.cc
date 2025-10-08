/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/content_install_dialog_qt.h"

#include <QDesktopServices>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QImage>
#include <QMessageBox>
#include <QPixmap>
#include <QScrollArea>
#include <QUrl>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/system.h"
#include "xenia/xbox.h"

namespace xe {
namespace ui {

ContentInstallDialogQt::ContentInstallDialogQt(
    QWidget* parent, const std::filesystem::path& content_root,
    std::shared_ptr<std::vector<Emulator::ContentInstallEntry>> entries)
    : QDialog(parent),
      content_root_(content_root),
      installation_entries_(entries) {
  SetupUI();

  // Update every 100ms
  update_timer_ = new QTimer(this);
  connect(update_timer_, &QTimer::timeout, this,
          &ContentInstallDialogQt::UpdateProgress);
  update_timer_->start(100);
}

ContentInstallDialogQt::~ContentInstallDialogQt() {
  if (update_timer_) {
    update_timer_->stop();
  }
}

void ContentInstallDialogQt::SetupUI() {
  setWindowTitle("Installation Progress");
  setModal(true);
  setAttribute(Qt::WA_DeleteOnClose);

  auto* main_layout = new QVBoxLayout(this);

  // Create table widget with 1 column per entry
  table_widget_ = new QTableWidget(this);
  table_widget_->setColumnCount(1);
  table_widget_->setRowCount(static_cast<int>(installation_entries_->size()));
  table_widget_->horizontalHeader()->setVisible(false);
  table_widget_->verticalHeader()->setVisible(false);
  table_widget_->setShowGrid(false);
  table_widget_->setSelectionMode(QAbstractItemView::NoSelection);
  table_widget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_widget_->horizontalHeader()->setStretchLastSection(true);

  // Populate table for each entry
  for (size_t i = 0; i < installation_entries_->size(); i++) {
    auto& entry = (*installation_entries_)[i];

    // Create a container widget for the entire row
    auto* row_widget = new QWidget();
    auto* row_layout = new QHBoxLayout(row_widget);
    row_layout->setContentsMargins(10, 10, 10, 10);
    row_layout->setSpacing(15);

    // Icon label - will be sized dynamically based on row height
    auto* icon_label = new QLabel();
    icon_label->setMinimumSize(80, 80);
    icon_label->setAlignment(Qt::AlignCenter);

    // Load icon from PNG data if available
    if (!entry.icon_data_.empty()) {
      QImage image;
      if (image.loadFromData(entry.icon_data_.data(),
                             static_cast<int>(entry.icon_data_.size()))) {
        // Scale pixmap to a larger size while preserving aspect ratio
        QPixmap pixmap = QPixmap::fromImage(image);
        icon_label->setPixmap(pixmap.scaled(80, 80, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation));
      } else {
        // Fallback if PNG loading fails
        icon_label->setText("📦");
        QFont icon_font = icon_label->font();
        icon_font.setPointSize(icon_font.pointSize() * 3);
        icon_label->setFont(icon_font);
      }
    } else {
      // No icon data, show placeholder
      icon_label->setText("📦");
      QFont icon_font = icon_label->font();
      icon_font.setPointSize(icon_font.pointSize() * 3);
      icon_label->setFont(icon_font);
    }
    row_layout->addWidget(icon_label);

    // Info column
    auto* info_widget = new QWidget();
    auto* info_layout = new QVBoxLayout(info_widget);
    info_layout->setContentsMargins(0, 0, 0, 0);
    info_layout->setSpacing(2);

    auto* name_label = new QLabel();
    name_label->setTextFormat(Qt::PlainText);
    info_layout->addWidget(name_label);

    auto* path_label = new QLabel();
    path_label->setTextFormat(Qt::RichText);
    path_label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    path_label->setOpenExternalLinks(false);
    connect(
        path_label, &QLabel::linkActivated, this, [this, i](const QString&) {
          auto& entry = (*installation_entries_)[i];
          // Launch file explorer in a separate thread to avoid blocking the
          // UI
          std::thread path_open(LaunchFileExplorer,
                                content_root_ / entry.data_installation_path_);
          path_open.detach();
        });
    info_layout->addWidget(path_label);

    auto* type_label = new QLabel();
    info_layout->addWidget(type_label);

    auto* status_label = new QLabel();
    info_layout->addWidget(status_label);

    auto* progress_bar = new QProgressBar();
    progress_bar->setMinimum(0);
    progress_bar->setMaximum(100);
    info_layout->addWidget(progress_bar);

    row_layout->addWidget(info_widget, 1);

    table_widget_->setCellWidget(static_cast<int>(i), 0, row_widget);

    // Auto-resize row height to fit content
    table_widget_->resizeRowToContents(static_cast<int>(i));

    // Store widgets for later updates
    entry_widgets_.push_back({icon_label, name_label, path_label, type_label,
                              status_label, progress_bar});
  }

  main_layout->addWidget(table_widget_);

  // Button layout
  auto* button_layout = new QHBoxLayout();

  // Cancel button
  cancel_button_ = new QPushButton("Cancel", this);
  connect(cancel_button_, &QPushButton::clicked, this,
          &ContentInstallDialogQt::OnCancelClicked);
  button_layout->addWidget(cancel_button_);

  button_layout->addStretch();

  // Close button
  close_button_ = new QPushButton("Close", this);
  close_button_->setEnabled(false);
  connect(close_button_, &QPushButton::clicked, this,
          &ContentInstallDialogQt::OnCloseClicked);
  button_layout->addWidget(close_button_);

  main_layout->addLayout(button_layout);

  setMinimumSize(600, 400);

  // Initial update
  UpdateProgress();
}

void ContentInstallDialogQt::UpdateProgress() {
  bool is_everything_installed = true;
  bool has_cancelled_entries = false;
  bool cancellation_complete = false;

  for (size_t i = 0; i < installation_entries_->size(); i++) {
    auto& entry = (*installation_entries_)[i];
    auto& widgets = entry_widgets_[i];

    // Check if this entry was cancelled and has finished
    if (entry.cancelled_.load() &&
        entry.installation_state_ == xe::Emulator::InstallState::failed &&
        entry.installation_result_ == X_ERROR_CANCELLED) {
      has_cancelled_entries = true;
    }

    // Update name
    widgets.name_label->setText(
        QString("Name: %1").arg(QString::fromStdString(entry.name_)));

    // Update path with link
    QString path_str =
        QString::fromStdString(xe::path_to_utf8(entry.data_installation_path_));
    widgets.path_label->setText(
        QString("Installation Path: <a href=\"%1\">%1</a>").arg(path_str));

    // Update content type
    if (entry.content_type_ != xe::XContentType::kInvalid) {
      auto it = XContentTypeMap.find(entry.content_type_);
      if (it != XContentTypeMap.end()) {
        widgets.type_label->setText(
            QString("Content Type: %1")
                .arg(QString::fromStdString(it->second)));
      }
    } else {
      widgets.type_label->setText("");
    }

    // Update status
    std::string result = fmt::format(
        "Status: {}", xe::Emulator::installStateStringName[static_cast<uint8_t>(
                          entry.installation_state_)]);

    if (entry.installation_state_ == xe::Emulator::InstallState::failed) {
      result += fmt::format(" - {} ({:08X})",
                            entry.installation_error_message_.c_str(),
                            entry.installation_result_);
    }

    widgets.status_label->setText(QString::fromStdString(result));

    // Update progress bar
    if (entry.content_size_ > 0) {
      int progress = static_cast<int>(
          (static_cast<double>(entry.currently_installed_size_) /
           entry.content_size_) *
          100.0);
      widgets.progress_bar->setValue(progress);

      if (entry.currently_installed_size_ != entry.content_size_ &&
          entry.installation_result_ == X_ERROR_SUCCESS &&
          !entry.cancelled_.load()) {
        is_everything_installed = false;
      }
    } else {
      widgets.progress_bar->setValue(0);
      // If content_size is still 0, headers haven't been processed yet
      // So not everything is installed (unless it's been cancelled)
      if (entry.installation_state_ != Emulator::InstallState::failed &&
          !entry.cancelled_.load()) {
        is_everything_installed = false;
      }
    }
  }

  // Check if cancellation is complete (all cancelled entries are now failed)
  if (has_cancelled_entries && !cancel_button_->isEnabled()) {
    bool all_cancelled_complete = true;
    for (const auto& entry : *installation_entries_) {
      if (entry.cancelled_.load()) {
        if (entry.installation_state_ != xe::Emulator::InstallState::failed) {
          all_cancelled_complete = false;
          break;
        }
      }
    }
    cancellation_complete = all_cancelled_complete;
  }

  // Show cancellation dialog and remove cancelled entries
  if (cancellation_complete && !cancellation_dialog_shown_) {
    cancellation_dialog_shown_ = true;

    // Count cancelled entries
    int cancelled_count = 0;
    for (const auto& entry : *installation_entries_) {
      if (entry.cancelled_.load() &&
          entry.installation_result_ == X_ERROR_CANCELLED) {
        cancelled_count++;
      }
    }

    // Show dialog
    QMessageBox::information(
        this, "Installation Cancelled",
        QString("Cancelled %1 installation(s) and cleaned up partial files.")
            .arg(cancelled_count));

    // Remove cancelled entries from the vector and UI
    for (int i = static_cast<int>(installation_entries_->size()) - 1; i >= 0;
         i--) {
      auto& entry = (*installation_entries_)[i];
      if (entry.cancelled_.load() &&
          entry.installation_result_ == X_ERROR_CANCELLED) {
        // Remove from table
        table_widget_->removeRow(i);
        // Remove from entry_widgets_
        entry_widgets_.erase(entry_widgets_.begin() + i);
        // Remove from installation_entries_
        installation_entries_->erase(installation_entries_->begin() + i);
      }
    }

    // Reset cancel button
    cancel_button_->setText("Cancel");
    cancel_button_->setEnabled(true);
    cancellation_dialog_shown_ = false;  // Reset for future cancellations
  }

  // Enable close button when everything is done, disable cancel
  close_button_->setEnabled(is_everything_installed);
  if (!cancellation_complete) {
    cancel_button_->setEnabled(!is_everything_installed);
  }

  // Show success dialog when all installations complete (detect transition)
  if (is_everything_installed && !was_everything_installed_ &&
      !success_dialog_shown_ && !installation_entries_->empty()) {
    success_dialog_shown_ = true;

    // Count successful installations
    int success_count = 0;
    for (const auto& entry : *installation_entries_) {
      if (entry.installation_state_ == Emulator::InstallState::installed) {
        success_count++;
      }
    }

    // Show dialog immediately on completion
    if (success_count > 0) {
      QMessageBox::information(
          this, "Installation Complete",
          QString("Successfully installed %1 package(s)!").arg(success_count));
    }
  }

  // Track previous state for next update
  was_everything_installed_ = is_everything_installed;

  // Auto-close if everything is done and user closed the dialog
  if (is_everything_installed && !isVisible()) {
    deleteLater();
  }
}

bool ContentInstallDialogQt::IsEverythingInstalled() {
  for (const auto& entry : *installation_entries_) {
    if (entry.content_size_ > 0) {
      if (entry.currently_installed_size_ != entry.content_size_ &&
          entry.installation_result_ == X_ERROR_SUCCESS &&
          !entry.cancelled_.load()) {
        return false;
      }
    } else {
      // If content_size is still 0, headers haven't been processed yet
      if (entry.installation_state_ != Emulator::InstallState::failed &&
          !entry.cancelled_.load()) {
        return false;
      }
    }
  }
  return true;
}

void ContentInstallDialogQt::OnCloseClicked() {
  if (IsEverythingInstalled()) {
    accept();
  }
}

void ContentInstallDialogQt::OnCancelClicked() {
  // Set the cancelled flag for all entries that are still in progress
  for (auto& entry : *installation_entries_) {
    if (entry.installation_state_ == Emulator::InstallState::preparing ||
        entry.installation_state_ == Emulator::InstallState::pending ||
        entry.installation_state_ == Emulator::InstallState::installing) {
      entry.cancelled_.store(true);
    }
  }

  // Disable the cancel button
  cancel_button_->setEnabled(false);
  cancel_button_->setText("Cancelling...");
}

}  // namespace ui
}  // namespace xe
