/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#import "xenia/ui/ios/launcher/ios_content_management.h"

#include <algorithm>
#include <cstdio>
#include <system_error>

#import "xenia/ui/ios/shared/ios_system_utils.h"
#include "xenia/vfs/stfs_metadata.h"

bool HasContentSidecarDataDirectory(const std::filesystem::path& path) {
  std::filesystem::path sidecar_path = path;
  sidecar_path += ".data";
  std::error_code ec;
  return std::filesystem::is_directory(sidecar_path, ec) && !ec;
}

std::filesystem::path xe_title_content_root(uint32_t title_id) {
  char title_id_buffer[9] = {};
  std::snprintf(title_id_buffer, sizeof(title_id_buffer), "%08X", title_id);
  return xe_get_ios_documents_path() / "content" / "0000000000000000" / title_id_buffer;
}

std::filesystem::path xe_title_update_content_root(uint32_t title_id) {
  return xe_title_content_root(title_id) / "000B0000";
}

std::filesystem::path xe_dlc_content_root(uint32_t title_id) {
  return xe_title_content_root(title_id) / "00000002";
}

NSString* xe_installed_content_kind_label(IOSInstalledContentKind kind) {
  switch (kind) {
    case IOSInstalledContentKind::kTitleUpdate:
      return @"Title Update";
    case IOSInstalledContentKind::kDlc:
      return @"DLC";
  }
  return @"Content";
}

std::string xe_content_package_directory_name(const std::filesystem::path& package_path) {
  std::string name = package_path.stem().string();
  if (name.empty()) {
    name = package_path.filename().string();
  }
  return name;
}

bool xe_read_selected_content_package(const std::filesystem::path& path,
                                      IOSSelectedContentPackage* package_out,
                                      NSString** error_message_out) {
  if (error_message_out) {
    *error_message_out = nil;
  }

  auto metadata = xe::vfs::ExtractStfsMetadata(path);
  if (!metadata.has_value()) {
    if (error_message_out) {
      *error_message_out = @"Could not read the content package header.";
    }
    return false;
  }

  if (metadata->data_file_count > 0 && !HasContentSidecarDataDirectory(path)) {
    if (error_message_out) {
      *error_message_out = @"This content package is missing its required .data sidecar folder.";
    }
    return false;
  }

  if (package_out) {
    package_out->title_id = metadata->title_id;
    package_out->content_type = static_cast<xe::XContentType>(metadata->content_type);
    package_out->path = path;
  }
  return true;
}

bool xe_copy_directory_recursive(const std::filesystem::path& source,
                                 const std::filesystem::path& destination,
                                 std::string* error_message_out) {
  std::error_code ec;
  std::filesystem::remove_all(destination, ec);
  ec.clear();
  std::filesystem::copy(
      source, destination,
      std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
      ec);
  if (ec) {
    if (error_message_out) {
      *error_message_out = ec.message();
    }
    return false;
  }
  return true;
}

bool xe_copy_content_package_into_root(const IOSSelectedContentPackage& package_info,
                                       const std::filesystem::path& destination_root,
                                       std::string* error_message_out) {
  if (error_message_out) {
    *error_message_out = "";
  }

  const std::filesystem::path package_directory =
      destination_root / xe_content_package_directory_name(package_info.path);
  std::error_code ec;
  std::filesystem::create_directories(package_directory, ec);
  if (ec) {
    if (error_message_out) {
      *error_message_out = "Failed creating content folder: " + ec.message();
    }
    return false;
  }

  const std::filesystem::path destination_file = package_directory / package_info.path.filename();
  std::filesystem::copy_file(package_info.path, destination_file,
                             std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    if (error_message_out) {
      *error_message_out = "Failed copying package: " + ec.message();
    }
    return false;
  }

  if (HasContentSidecarDataDirectory(package_info.path)) {
    std::filesystem::path source_sidecar = package_info.path;
    source_sidecar += ".data";
    std::filesystem::path destination_sidecar = destination_file;
    destination_sidecar += ".data";
    if (!xe_copy_directory_recursive(source_sidecar, destination_sidecar, error_message_out)) {
      return false;
    }
  }

  return true;
}

void xe_collect_installed_content(const std::filesystem::path& root, IOSInstalledContentKind kind,
                                  std::vector<IOSInstalledContentEntry>* content_out) {
  if (!content_out) {
    return;
  }

  std::error_code ec;
  if (!std::filesystem::exists(root, ec)) {
    return;
  }

  std::filesystem::directory_iterator it(root, ec);
  std::filesystem::directory_iterator end;
  for (; !ec && it != end; ++it) {
    if (!it->is_directory(ec) || ec) {
      ec.clear();
      continue;
    }

    IOSInstalledContentEntry entry;
    entry.kind = kind;
    entry.name = it->path().filename().string();
    entry.path = it->path();
    content_out->push_back(std::move(entry));
  }
}

std::vector<IOSInstalledContentEntry> xe_list_installed_content(uint32_t title_id) {
  std::vector<IOSInstalledContentEntry> content;
  if (!title_id) {
    return content;
  }

  xe_collect_installed_content(xe_title_update_content_root(title_id),
                               IOSInstalledContentKind::kTitleUpdate, &content);
  xe_collect_installed_content(xe_dlc_content_root(title_id), IOSInstalledContentKind::kDlc,
                               &content);
  std::sort(content.begin(), content.end(),
            [](const IOSInstalledContentEntry& a, const IOSInstalledContentEntry& b) {
              if (a.kind != b.kind) {
                return a.kind < b.kind;
              }
              return a.name < b.name;
            });
  return content;
}
