/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>
#include <iostream>

#include "xenia/base/console_app_main.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/vfs/iso_metadata.h"
#include "xenia/vfs/stfs_metadata.h"
#include "xenia/vfs/xex_metadata.h"
#include "xenia/vfs/zar_metadata.h"

namespace xe {
namespace vfs {

DEFINE_transient_path(
    target, "",
    "Path to game file (XEX, ISO, ZAR, STFS) to extract metadata from.",
    "General");

namespace {

std::string ToLower(const std::string& s) {
  std::string result = s;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

void OutputWithFormat(const toml::table& table, const std::string& format) {
  toml::table root;
  root.insert("source_format", format);

  // Copy all entries from the original table.
  for (const auto& [key, value] : table) {
    root.insert(key, value);
  }

  std::cout << root << std::endl;
}

}  // namespace

int xex_metadata_main(const std::vector<std::string>& args) {
  if (cvars::target.empty()) {
    XELOGE("Usage: {} <path-to-game-file>", args[0]);
    XELOGE("Supported formats: XEX, ISO, ZAR, STFS/SVOD containers");
    return 1;
  }

  std::string ext = ToLower(cvars::target.extension().string());

  // Try to extract based on extension, with fallback.
  if (ext == ".iso") {
    if (auto metadata = ExtractIsoMetadata(cvars::target)) {
      OutputWithFormat(metadata->ToToml(), "iso");
      return 0;
    }
  } else if (ext == ".zar") {
    if (auto metadata = ExtractZarMetadata(cvars::target)) {
      OutputWithFormat(metadata->ToToml(), "zar");
      return 0;
    }
  } else if (ext == ".xex") {
    if (auto metadata = ExtractXexMetadata(cvars::target)) {
      OutputWithFormat(metadata->ToToml(), "xex");
      return 0;
    }
  } else {
    // Unknown extension - try all formats.
    // Try XEX first (smallest header check).
    if (auto metadata = ExtractXexMetadata(cvars::target)) {
      OutputWithFormat(metadata->ToToml(), "xex");
      return 0;
    }
    // Try STFS/SVOD container.
    if (auto metadata = ExtractStfsMetadata(cvars::target)) {
      OutputWithFormat(metadata->ToToml(), "stfs");
      return 0;
    }
    // Try ZAR.
    if (auto metadata = ExtractZarMetadata(cvars::target)) {
      OutputWithFormat(metadata->ToToml(), "zar");
      return 0;
    }
    // Try ISO.
    if (auto metadata = ExtractIsoMetadata(cvars::target)) {
      OutputWithFormat(metadata->ToToml(), "iso");
      return 0;
    }
  }

  XELOGE("Failed to extract metadata from: {}", cvars::target.string());
  return 1;
}

}  // namespace vfs
}  // namespace xe

XE_DEFINE_CONSOLE_APP("xenia-xex-metadata", xe::vfs::xex_metadata_main,
                      "<path-to-game-file>", "target");
