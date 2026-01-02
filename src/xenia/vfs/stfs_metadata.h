/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_STFS_METADATA_H_
#define XENIA_VFS_STFS_METADATA_H_

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "third_party/tomlplusplus/toml.hpp"

namespace xe {
namespace vfs {

struct StfsMetadata {
  uint32_t title_id = 0;
  uint32_t media_id = 0;
  uint32_t savegame_id = 0;
  uint32_t content_type = 0;
  uint64_t content_size = 0;

  std::string display_name;
  std::string description;
  std::string publisher;
  std::string title_name;

  uint8_t disc_number = 0;
  uint8_t disc_count = 0;

  toml::table ToToml() const;
};

std::optional<StfsMetadata> ExtractStfsMetadata(
    const std::filesystem::path& path);

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_STFS_METADATA_H_
