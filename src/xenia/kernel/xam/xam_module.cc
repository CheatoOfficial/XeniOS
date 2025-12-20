/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/xam_module.h"

#include <fstream>
#include <iomanip>
#include <sstream>

#include "xenia/base/math.h"
#include "xenia/base/string_util.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/xam_private.h"

namespace xe {
namespace kernel {
namespace xam {

XamModule::XamModule(Emulator* emulator, KernelState* kernel_state)
    : KernelModule(kernel_state, "xe:\\xam.xex"), loader_data_() {
  RegisterExportTable(export_resolver_);

  // Register all exported functions.
#define XE_MODULE_EXPORT_GROUP(m, n) \
  Register##n##Exports(export_resolver_, kernel_state_);
#include "xam_module_export_groups.inc"
#undef XE_MODULE_EXPORT_GROUP
}

static auto& get_xam_exports() {
  static std::vector<xe::cpu::Export*> xam_exports(4096);
  return xam_exports;
}

xe::cpu::Export* RegisterExport_xam(xe::cpu::Export* export_entry) {
  auto& xam_exports = get_xam_exports();
  assert_true(export_entry->ordinal < xam_exports.size());
  xam_exports[export_entry->ordinal] = export_entry;
  return export_entry;
}
// Build the export table used for resolution.
#include "xenia/kernel/util/export_table_pre.inc"
static constexpr xe::cpu::Export xam_export_table[] = {
#include "xenia/kernel/xam/xam_table.inc"
};
#include "xenia/kernel/util/export_table_post.inc"
void XamModule::RegisterExportTable(xe::cpu::ExportResolver* export_resolver) {
  assert_not_null(export_resolver);
  auto& xam_exports = get_xam_exports();

  for (size_t i = 0; i < xe::countof(xam_export_table); ++i) {
    auto& export_entry = xam_export_table[i];
    assert_true(export_entry.ordinal < xam_exports.size());
    if (!xam_exports[export_entry.ordinal]) {
      xam_exports[export_entry.ordinal] =
          const_cast<xe::cpu::Export*>(&export_entry);
    }
  }
  export_resolver->RegisterTable("xam.xex", &get_xam_exports());
}

XamModule::~XamModule() {}

void XamModule::LoadLoaderData() {
  std::filesystem::path file_path(kXamModuleLoaderDataFileName);
  std::ifstream file(file_path);

  if (!file.is_open()) {
    loader_data_.launch_data_present = false;
    return;
  }

  loader_data_.launch_data_present = true;

  std::string line;
  while (std::getline(file, line)) {
    // Find the '=' delimiter
    size_t eq_pos = line.find('=');
    if (eq_pos == std::string::npos) {
      continue;  // Skip malformed lines
    }

    std::string key = line.substr(0, eq_pos);
    std::string value = line.substr(eq_pos + 1);

    if (key == "host_path") {
      loader_data_.host_path = value;
    } else if (key == "launch_path") {
      loader_data_.launch_path = value;
    } else if (key == "launch_flags") {
      loader_data_.launch_flags = std::stoul(value);
    } else if (key == "launch_data") {
      // Convert hex string back to bytes
      if (!value.empty()) {
        loader_data_.launch_data.clear();
        for (size_t i = 0; i < value.length(); i += 2) {
          if (i + 1 < value.length()) {
            std::string byte_str = value.substr(i, 2);
            uint8_t byte =
                static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
            loader_data_.launch_data.push_back(byte);
          }
        }
      }
    }
  }

  file.close();
  // We read launch data. Let's remove it till next request.
  std::filesystem::remove(kXamModuleLoaderDataFileName);
}

void XamModule::SaveLoaderData() {
  std::filesystem::path file_path(kXamModuleLoaderDataFileName);
  std::ofstream file(file_path);

  if (!file.is_open()) {
    return;
  }

  std::filesystem::path host_path = loader_data_.host_path;
  std::string launch_path = loader_data_.launch_path;

  auto remove_prefix = [&launch_path](std::string_view prefix) {
    if (launch_path.compare(0, prefix.length(), prefix) == 0) {
      launch_path = launch_path.substr(prefix.length());
    }
  };

  remove_prefix("game:\\");
  remove_prefix("d:\\");

  if (host_path.extension() == ".xex") {
    host_path.remove_filename();
    host_path = host_path / launch_path;
    launch_path = "";
  }

  const std::string host_path_as_string = xe::path_to_utf8(host_path);

  // Write text format: one field per line
  file << "host_path=" << host_path_as_string << "\n";
  file << "launch_path=" << launch_path << "\n";
  file << "launch_flags=" << loader_data_.launch_flags << "\n";
  file << "title_id=" << std::hex << kernel_state()->title_id() << "\n";

  // Convert launch_data bytes to hex string
  if (!loader_data_.launch_data.empty()) {
    file << "launch_data=";
    for (uint8_t byte : loader_data_.launch_data) {
      file << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<unsigned>(byte);
    }
    file << "\n";
  } else {
    file << "launch_data=\n";
  }

  file.close();
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
