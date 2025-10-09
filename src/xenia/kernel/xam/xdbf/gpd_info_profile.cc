/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/xdbf/gpd_info_profile.h"

#include "xenia/base/string_util.h"

#include <ranges>

namespace xe {
namespace kernel {
namespace xam {

const std::vector<const X_XDBF_GPD_TITLE_PLAYED*>
GpdInfoProfile::GetTitlesInfo() const {
  std::vector<const X_XDBF_GPD_TITLE_PLAYED*> entries;

  auto titles = entries_ | std::views::filter([](const auto& entry) {
                  return !IsSyncEntry(&entry);
                }) |
                std::views::filter([](const auto& entry) {
                  return IsEntryOfSection(&entry, GpdSection::kTitle);
                });

  for (const auto& title : titles) {
    entries.push_back(
        reinterpret_cast<const X_XDBF_GPD_TITLE_PLAYED*>(title.data.data()));
  }
  return entries;
};

X_XDBF_GPD_TITLE_PLAYED* GpdInfoProfile::GetTitleInfo(const uint32_t title_id) {
  auto title = entries_ | std::views::filter([](const auto& entry) {
                 return !IsSyncEntry(&entry);
               }) |
               std::views::filter([](const auto& entry) {
                 return IsEntryOfSection(&entry, GpdSection::kTitle);
               }) |
               std::views::filter([title_id](const auto& entry) {
                 return static_cast<uint32_t>(entry.info.id) == title_id;
               });

  if (title.empty()) {
    return nullptr;
  }

  return reinterpret_cast<X_XDBF_GPD_TITLE_PLAYED*>(title.begin()->data.data());
}

std::u16string GpdInfoProfile::GetTitleName(const uint32_t title_id) const {
  const Entry* entry =
      GetEntry(static_cast<uint16_t>(GpdSection::kTitle), title_id);

  if (!entry) {
    return std::u16string();
  }

  return string_util::read_u16string_and_swap(reinterpret_cast<const char16_t*>(
      entry->data.data() + sizeof(X_XDBF_GPD_TITLE_PLAYED)));
}

void GpdInfoProfile::AddNewTitle(const SpaInfo* title_data) {
  const X_XDBF_GPD_TITLE_PLAYED title_gpd_data =
      FillTitlePlayedData(title_data);

  const std::u16string title_name = xe::to_utf16(title_data->title_name());

  const uint32_t entry_size =
      sizeof(X_XDBF_GPD_TITLE_PLAYED) +
      static_cast<uint32_t>(string_util::size_in_bytes(title_name));

  Entry entry(title_data->title_id(), static_cast<uint16_t>(GpdSection::kTitle),
              entry_size);

  memcpy(entry.data.data(), &title_gpd_data, sizeof(X_XDBF_GPD_TITLE_PLAYED));

  string_util::copy_and_swap_truncating(
      reinterpret_cast<char16_t*>(entry.data.data() +
                                  sizeof(X_XDBF_GPD_TITLE_PLAYED)),
      title_name, title_name.size() + 1);

  UpsertEntry(&entry);
}

bool GpdInfoProfile::RemoveTitle(const uint32_t title_id) {
  const Entry* entry =
      GetEntry(static_cast<uint16_t>(GpdSection::kTitle), title_id);
  if (!entry) {
    return false;
  }

  DeleteEntry(entry);
  return true;
}

X_XDBF_GPD_TITLE_PLAYED GpdInfoProfile::FillTitlePlayedData(
    const SpaInfo* title_data) const {
  X_XDBF_GPD_TITLE_PLAYED title_gpd_data = {};

  title_gpd_data.title_id = title_data->title_id();
  title_gpd_data.achievements_count = title_data->achievement_count();
  title_gpd_data.gamerscore_total = title_data->total_gamerscore();

  return title_gpd_data;
}

void GpdInfoProfile::UpdateTitleInfo(const uint32_t title_id,
                                     X_XDBF_GPD_TITLE_PLAYED* title_data) {
  X_XDBF_GPD_TITLE_PLAYED* current_info = GetTitleInfo(title_id);
  if (!current_info) {
    return;
  }

  memcpy(current_info, title_data, sizeof(X_XDBF_GPD_TITLE_PLAYED));
}

// Xenia-specific: Use custom string IDs to store file paths
// String IDs 0xFFFE0000 - 0xFFFEFFFF are reserved for Xenia extensions
// For multi-disc titles, we store all paths in a single string separated by
// newline (newline is invalid in filenames on both Windows and Linux) Format:
// "path/to/disc1.iso\npath/to/disc2.iso\npath/to/disc3.iso"
constexpr uint32_t kXeniaPathStringBase = 0xFFFE0000;
constexpr char kPathDelimiter = '\n';

void GpdInfoProfile::SetTitlePath(uint32_t title_id,
                                  const std::filesystem::path& path) {
  const uint32_t string_id = kXeniaPathStringBase + title_id;
  const std::u16string path_u16 = xe::to_utf16(xe::path_to_utf8(path));
  AddString(string_id, path_u16);
}

void GpdInfoProfile::AddTitlePath(uint32_t title_id,
                                  const std::filesystem::path& path) {
  const uint32_t string_id = kXeniaPathStringBase + title_id;
  std::u16string existing_paths_u16 = GetString(string_id);

  if (existing_paths_u16.empty()) {
    // No paths yet, this shouldn't happen in normal flow but handle it
    SetTitlePath(title_id, path);
    return;
  }

  // Convert to UTF-8 for easier manipulation
  std::string existing_paths = xe::to_utf8(existing_paths_u16);
  std::string new_path = xe::path_to_utf8(path);

  // Append new path with newline delimiter
  existing_paths += kPathDelimiter;
  existing_paths += new_path;

  // Store back
  const std::u16string combined_u16 = xe::to_utf16(existing_paths);
  AddString(string_id, combined_u16);
}

std::optional<std::filesystem::path> GpdInfoProfile::GetTitlePath(
    uint32_t title_id) const {
  const uint32_t string_id = kXeniaPathStringBase + title_id;
  const std::u16string path_u16 = GetString(string_id);

  if (path_u16.empty()) {
    return std::nullopt;
  }

  // Get the first path (before any newline)
  std::string paths_utf8 = xe::to_utf8(path_u16);
  size_t delimiter_pos = paths_utf8.find(kPathDelimiter);
  if (delimiter_pos != std::string::npos) {
    paths_utf8 = paths_utf8.substr(0, delimiter_pos);
  }

  return std::filesystem::path(paths_utf8);
}

std::vector<std::filesystem::path> GpdInfoProfile::GetTitlePaths(
    uint32_t title_id) const {
  std::vector<std::filesystem::path> paths;

  const uint32_t string_id = kXeniaPathStringBase + title_id;
  const std::u16string path_u16 = GetString(string_id);

  if (path_u16.empty()) {
    return paths;
  }

  // Split by newline delimiter
  std::string paths_utf8 = xe::to_utf8(path_u16);
  size_t start = 0;
  size_t end = 0;

  while ((end = paths_utf8.find(kPathDelimiter, start)) != std::string::npos) {
    std::string path_str = paths_utf8.substr(start, end - start);
    if (!path_str.empty()) {
      paths.push_back(std::filesystem::path(path_str));
    }
    start = end + 1;
  }

  // Add the last path (or only path if no delimiter)
  if (start < paths_utf8.length()) {
    std::string path_str = paths_utf8.substr(start);
    if (!path_str.empty()) {
      paths.push_back(std::filesystem::path(path_str));
    }
  }

  return paths;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
