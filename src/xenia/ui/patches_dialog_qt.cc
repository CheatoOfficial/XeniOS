/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/patches_dialog_qt.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QSpacerItem>
#include <fstream>
#include <regex>

#include "third_party/fmt/include/fmt/format.h"
#include "third_party/tomlplusplus/toml.hpp"
#include "xenia/app/emulator_window.h"
#include "xenia/base/logging.h"
#include "xenia/ui/qt_util.h"

namespace xe {
namespace app {

using xe::ui::SafeQString;

PatchesDialogQt::PatchesDialogQt(QWidget* parent,
                                 EmulatorWindow* emulator_window,
                                 uint32_t title_id,
                                 const std::filesystem::path& patch_file)
    : QDialog(parent),
      emulator_window_(emulator_window),
      title_id_(title_id),
      patch_file_(patch_file) {
  SetupUI();
  LoadPatchFile();
}

PatchesDialogQt::~PatchesDialogQt() = default;

void PatchesDialogQt::SetupUI() {
  setWindowTitle("Patch Manager");
  setMinimumSize(600, 400);
  resize(700, 500);

  auto* main_layout = new QVBoxLayout(this);

  // Extract display name from patch file
  std::string filename = patch_file_.filename().string();
  std::string display_name = filename;

  // Remove title ID prefix and " - "
  size_t dash_pos = filename.find(" - ");
  if (dash_pos != std::string::npos) {
    display_name = filename.substr(dash_pos + 3);
  }

  // Remove ".patch.toml" suffix
  size_t suffix_pos = display_name.rfind(".patch.toml");
  if (suffix_pos != std::string::npos) {
    display_name = display_name.substr(0, suffix_pos);
  }

  // Title label
  auto* title_label = new QLabel(SafeQString(display_name), this);
  QFont title_font = title_label->font();
  title_font.setPointSize(title_font.pointSize() + 2);
  title_font.setBold(true);
  title_label->setFont(title_font);
  main_layout->addWidget(title_label);

  // Info label
  info_label_ = new QLabel(
      "Enable or disable individual patches. Changes take effect on next game "
      "launch.",
      this);
  info_label_->setWordWrap(true);
  info_label_->setStyleSheet("color: gray;");
  main_layout->addWidget(info_label_);

  // Scroll area for patches
  scroll_area_ = new QScrollArea(this);
  scroll_area_->setWidgetResizable(true);
  scroll_area_->setFrameShape(QFrame::NoFrame);

  patches_container_ = new QWidget(this);
  patches_layout_ = new QVBoxLayout(patches_container_);
  patches_layout_->setAlignment(Qt::AlignTop);

  scroll_area_->setWidget(patches_container_);
  main_layout->addWidget(scroll_area_, 1);

  // Close button
  auto* button_layout = new QHBoxLayout();
  button_layout->addStretch();

  auto* close_button = new QPushButton("Close", this);
  connect(close_button, &QPushButton::clicked, this, &QDialog::accept);
  button_layout->addWidget(close_button);

  main_layout->addLayout(button_layout);
}

std::filesystem::path PatchesDialogQt::GetStorageRootPatchPath() {
  if (!emulator_window_ || !emulator_window_->emulator()) {
    return patch_file_;
  }

  auto storage_patches_dir =
      emulator_window_->emulator()->storage_root() / "patches";
  return storage_patches_dir / patch_file_.filename();
}

void PatchesDialogQt::LoadPatchFile() {
  patches_.clear();

  if (!std::filesystem::exists(patch_file_)) {
    XELOGE("Patch file does not exist: {}", patch_file_.filename().string());
    return;
  }

  try {
    auto patch_toml = toml::parse_file(patch_file_.string());

    // Check if there's a patch array
    if (!patch_toml.contains("patch")) {
      XELOGE("No patches found in file: {}", patch_file_.filename().string());
      return;
    }

    auto patches_array = patch_toml["patch"].as_array();
    if (!patches_array) {
      XELOGE("'patch' is not an array in file: {}",
             patch_file_.filename().string());
      return;
    }

    size_t patch_index = 0;
    for (const auto& patch_entry : *patches_array) {
      PatchInfo info;
      info.patch_index = patch_index++;

      // Cast to table to access fields
      auto* patch_table = patch_entry.as_table();
      if (!patch_table) {
        continue;
      }

      // Get patch name
      if (auto name = (*patch_table)["name"].value<std::string>()) {
        info.name = *name;
      } else {
        info.name = fmt::format("Patch #{}", info.patch_index + 1);
      }

      // Get patch description
      if (auto desc = (*patch_table)["desc"].value<std::string>()) {
        info.description = *desc;
      }

      // Get patch author
      if (auto author = (*patch_table)["author"].value<std::string>()) {
        info.author = *author;
      }

      // Get enabled state (default to false if not specified)
      if (auto enabled = (*patch_table)["is_enabled"].value<bool>()) {
        info.is_enabled = *enabled;
      } else {
        info.is_enabled = false;
      }

      // Create checkbox for this patch
      info.checkbox = new QCheckBox(SafeQString(info.name), patches_container_);
      info.checkbox->setChecked(info.is_enabled);

      // Connect checkbox to save handler
      connect(info.checkbox, &QCheckBox::toggled, this,
              [this, patch_index = info.patch_index](bool checked) {
                SavePatchToggle(patch_index, checked);
              });

      patches_layout_->addWidget(info.checkbox);

      // Add description/author as a sub-label if available
      if (!info.description.empty() || !info.author.empty()) {
        auto* detail_label = new QLabel(patches_container_);
        QString detail_text;

        if (!info.description.empty()) {
          detail_text = SafeQString(info.description);
        }

        if (!info.author.empty()) {
          if (!detail_text.isEmpty()) {
            detail_text += "\n";
          }
          detail_text += QString("Author: %1").arg(SafeQString(info.author));
        }

        detail_label->setText(detail_text);
        detail_label->setWordWrap(true);
        detail_label->setStyleSheet(
            "color: gray; margin-left: 20px; margin-bottom: 10px;");
        patches_layout_->addWidget(detail_label);
      }

      patches_.push_back(std::move(info));
    }

    if (patches_.empty()) {
      auto* no_patches_label =
          new QLabel("No patches found in this file.", patches_container_);
      no_patches_label->setStyleSheet("color: gray;");
      patches_layout_->addWidget(no_patches_label);
    }

  } catch (const toml::parse_error& err) {
    XELOGE("Failed to parse patch file {}: {}", patch_file_.filename().string(),
           err.what());

    auto* error_label =
        new QLabel(QString("Error loading patches: %1").arg(err.what()),
                   patches_container_);
    error_label->setStyleSheet("color: red;");
    patches_layout_->addWidget(error_label);
  }
}

bool PatchesDialogQt::UpdateSinglePatchEnabledLine(
    std::vector<std::string>& lines, size_t patch_index, bool new_value) {
  // Regex patterns for matching TOML structures
  static const std::regex patch_header_regex(R"(^\s*\[\[patch\]\]\s*$)");
  static const std::regex section_header_regex(R"(^\s*\[([^\[\]]+)\]\s*$)");
  static const std::regex is_enabled_regex(R"(^\s*is_enabled\s*=\s*(.+))");
  static const std::regex comment_line_regex(R"(^\s*#.*)");

  // Find all is_enabled lines in patch blocks
  std::vector<size_t> enabled_lines;
  bool in_patch_block = false;

  for (size_t i = 0; i < lines.size(); ++i) {
    const std::string& line = lines[i];

    // Skip comment lines
    if (std::regex_match(line, comment_line_regex)) {
      continue;
    }

    // Check for patch section marker
    if (std::regex_match(line, patch_header_regex)) {
      in_patch_block = true;
      continue;
    }

    // Check if we've hit another top-level section (not an array element)
    if (std::regex_match(line, section_header_regex)) {
      in_patch_block = false;
      continue;
    }

    // Look for is_enabled field within patch blocks
    if (in_patch_block) {
      if (std::regex_search(line, is_enabled_regex)) {
        enabled_lines.push_back(i);
      }
    }
  }

  // Check if the patch index is valid
  if (patch_index >= enabled_lines.size()) {
    XELOGE("Patch index {} out of range (found {} is_enabled fields)",
           patch_index, enabled_lines.size());
    return false;
  }

  // Update only the specific is_enabled line for this patch
  size_t line_idx = enabled_lines[patch_index];
  std::string& enabled_line = lines[line_idx];

  // Use regex to replace the value while preserving formatting
  static const std::regex value_regex(
      R"((^\s*is_enabled\s*=\s*)(true|false)(.*)$)");
  std::smatch match;

  if (std::regex_match(enabled_line, match, value_regex)) {
    // match[1] = everything before the value
    // match[2] = the current value (true/false)
    // match[3] = everything after the value (comments)
    lines[line_idx] =
        match[1].str() + (new_value ? "true" : "false") + match[3].str();
    return true;
  } else {
    XELOGE("Failed to parse is_enabled line for patch #{}", patch_index + 1);
    return false;
  }
}

void PatchesDialogQt::SavePatchToggle(size_t patch_index, bool new_value) {
  if (!std::filesystem::exists(patch_file_)) {
    XELOGE("Patch file does not exist: {}", patch_file_.filename().string());
    return;
  }

  try {
    auto storage_patch_path = GetStorageRootPatchPath();
    bool is_bundled_patch = (patch_file_ != storage_patch_path);

    // If this is a bundled patch, copy it to storage_root first
    if (is_bundled_patch && !std::filesystem::exists(storage_patch_path)) {
      auto storage_patches_dir = storage_patch_path.parent_path();

      // Create storage patches directory if it doesn't exist
      if (!std::filesystem::exists(storage_patches_dir)) {
        std::filesystem::create_directories(storage_patches_dir);
      }

      // Copy the bundled patch to storage_root
      std::filesystem::copy_file(
          patch_file_, storage_patch_path,
          std::filesystem::copy_options::overwrite_existing);

      XELOGI("Copied bundled patch to storage_root: {}",
             patch_file_.filename().string());
    }

    // Now read from and write to the storage_root version
    auto write_path = is_bundled_patch ? storage_patch_path : patch_file_;

    // Read the file content to preserve comments and formatting
    std::ifstream infile(write_path);
    if (!infile.is_open()) {
      XELOGE("Failed to open patch file for reading: {}",
             write_path.filename().string());
      return;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(infile, line)) {
      lines.push_back(line);
    }
    infile.close();

    // Update only the specific patch's is_enabled value
    if (!UpdateSinglePatchEnabledLine(lines, patch_index, new_value)) {
      XELOGE("Failed to update patch #{} enabled state in {}", patch_index + 1,
             write_path.filename().string());
      return;
    }

    // Write the modified content back to file
    std::ofstream outfile(write_path, std::ios::out | std::ios::trunc);
    if (!outfile.is_open()) {
      XELOGE("Failed to open patch file for writing: {}",
             write_path.filename().string());
      return;
    }

    for (size_t i = 0; i < lines.size(); ++i) {
      outfile << lines[i];
      if (i < lines.size() - 1) {
        outfile << "\n";
      }
    }
    outfile.close();

    XELOGI("Updated patch #{} in {}", patch_index + 1,
           write_path.filename().string());

    // Update info label to show changes pending
    info_label_->setText(
        "Changes saved. They will take effect on next game launch.");
    info_label_->setStyleSheet("color: #107C10;");  // Green color

  } catch (const std::exception& e) {
    XELOGE("Failed to save patch toggle: {}", e.what());

    info_label_->setText(QString("Error saving changes: %1").arg(e.what()));
    info_label_->setStyleSheet("color: red;");
  }
}

}  // namespace app
}  // namespace xe
