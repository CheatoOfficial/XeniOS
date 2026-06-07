/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/zar_metadata.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "third_party/zarchive/include/zarchive/zarchivereader.h"
#include "xenia/base/utf8.h"

namespace xe {
namespace vfs {

namespace {

// Recursively search for default.xex in the archive.
ZArchiveNodeHandle FindDefaultXex(ZArchiveReader* reader,
                                  const std::string& dir_path, int max_depth) {
  if (max_depth <= 0) {
    return ZARCHIVE_INVALID_NODE;
  }

  ZArchiveNodeHandle dir_handle = reader->LookUp(dir_path, false, true);
  if (dir_handle == ZARCHIVE_INVALID_NODE || !reader->IsDirectory(dir_handle)) {
    return ZARCHIVE_INVALID_NODE;
  }

  uint32_t entry_count = reader->GetDirEntryCount(dir_handle);
  for (uint32_t i = 0; i < entry_count; i++) {
    ZArchiveReader::DirEntry entry;
    if (!reader->GetDirEntry(dir_handle, i, entry)) {
      continue;
    }

    if (entry.isFile && xe::utf8::equal_case(entry.name, "default.xex")) {
      std::string file_path = dir_path;
      if (!file_path.empty() && file_path.back() != '/') {
        file_path += '/';
      }
      file_path += std::string(entry.name);
      return reader->LookUp(file_path, true, false);
    }

    if (entry.isDirectory) {
      std::string subdir_path = dir_path;
      if (!subdir_path.empty() && subdir_path.back() != '/') {
        subdir_path += '/';
      }
      subdir_path += std::string(entry.name);

      ZArchiveNodeHandle result =
          FindDefaultXex(reader, subdir_path, max_depth - 1);
      if (result != ZARCHIVE_INVALID_NODE) {
        return result;
      }
    }
  }

  return ZARCHIVE_INVALID_NODE;
}

}  // namespace

std::optional<XexMetadata> ExtractZarMetadata(
    const std::filesystem::path& path) {
  std::unique_ptr<ZArchiveReader> reader(ZArchiveReader::OpenFromFile(path));
  if (!reader) {
    return std::nullopt;
  }

  ZArchiveNodeHandle handle = FindDefaultXex(reader.get(), "/", 2);
  if (handle == ZARCHIVE_INVALID_NODE) {
    handle = FindDefaultXex(reader.get(), "", 2);
  }
  if (handle == ZARCHIVE_INVALID_NODE || !reader->IsFile(handle)) {
    return std::nullopt;
  }

  uint64_t file_size = reader->GetFileSize(handle);
  if (file_size < 24) {
    return std::nullopt;
  }

  // Read base header to get header_size (big-endian at offset 0x08).
  uint8_t base_header[24];
  if (reader->ReadFromFile(handle, 0, sizeof(base_header), base_header) !=
      sizeof(base_header)) {
    return std::nullopt;
  }

  uint32_t magic = (base_header[0] << 24) | (base_header[1] << 16) |
                   (base_header[2] << 8) | base_header[3];
  if (magic != 0x58455831 && magic != 0x58455832) {
    return std::nullopt;
  }

  uint32_t header_size = (base_header[8] << 24) | (base_header[9] << 16) |
                         (base_header[10] << 8) | base_header[11];
  if (header_size < 24 || header_size > file_size) {
    return std::nullopt;
  }

  // Read the whole embedded default.xex (capped via kMaxXexMetadataReadBytes),
  // not just the header region. Optional headers such as
  // XEX_HEADER_EXECUTION_INFO (which carries the title_id) are referenced by
  // offset and GetXexOptHeader does not bounds-check against the buffer, so a
  // header-only read could pull a truncated/garbled title_id. That then misses
  // the title-name cache and the UI falls back to the PE module name (e.g.
  // "default.pe" / "simpsonsfe.exe"). The ISO path already hands
  // ExtractXexMetadata the entire xex; match that here.
  const uint64_t read_size =
      std::min<uint64_t>(file_size, kMaxXexMetadataReadBytes);
  std::vector<uint8_t> xex_data(static_cast<size_t>(read_size));
  if (reader->ReadFromFile(handle, 0, read_size, xex_data.data()) !=
      read_size) {
    return std::nullopt;
  }

  return ExtractXexMetadata(xex_data.data(), xex_data.size());
}

}  // namespace vfs
}  // namespace xe
