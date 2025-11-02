/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/file_picker.h"

#include <string>

#include <QFileDialog>
#include <QString>

#include "xenia/base/assert.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/ui/qt_util.h"
#include "xenia/ui/window_qt.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

namespace xe {
namespace ui {

using xe::ui::SafeQString;

#if XE_PLATFORM_WIN32
// Detect if running under Wine by checking for wine_get_version in ntdll
static bool IsRunningOnWine() {
  static bool is_wine = []() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
      return GetProcAddress(ntdll, "wine_get_version") != nullptr;
    }
    return false;
  }();
  return is_wine;
}
#endif

class QtFilePicker : public FilePicker {
 public:
  QtFilePicker();
  ~QtFilePicker() override;

  bool Show(Window* parent_window) override;

 private:
};

std::unique_ptr<FilePicker> FilePicker::Create() {
  return std::make_unique<QtFilePicker>();
}

QtFilePicker::QtFilePicker() = default;

QtFilePicker::~QtFilePicker() = default;

bool QtFilePicker::Show(Window* parent_window) {
  QFileDialog::FileMode file_mode;
  QFileDialog::AcceptMode accept_mode;

  switch (mode()) {
    case Mode::kOpen:
      if (type() == Type::kFile) {
        file_mode = multi_selection() ? QFileDialog::ExistingFiles
                                      : QFileDialog::ExistingFile;
      } else {
        file_mode = QFileDialog::Directory;
      }
      accept_mode = QFileDialog::AcceptOpen;
      break;
    case Mode::kSave:
      file_mode = QFileDialog::AnyFile;
      accept_mode = QFileDialog::AcceptSave;
      break;
    default:
      XELOGE("QtFilePicker::Show: Unhandled mode: {}, Type: {}",
             static_cast<int>(mode()), static_cast<int>(type()));
      assert_always();
      return false;
  }

  // Use the custom title set via set_title(), or default based on mode
  QString title = SafeQString(this->title());

  auto* qt_window =
      parent_window ? dynamic_cast<QtWindow*>(parent_window) : nullptr;

  // Use static QFileDialog function to avoid Qt container ABI issues on Windows
  QString initial_dir;
  if (!initial_directory().empty()) {
    std::string dir_str = initial_directory().string();
    initial_dir =
        QString::fromUtf8(dir_str.c_str(), static_cast<int>(dir_str.size()));
  }

  // Don't apply file filters - show all files
  QString filter;

  // Force Qt's dialog when running on Wine, as native Windows dialogs don't
  // work well. Otherwise try native dialog.
  QFileDialog::Options options = QFileDialog::Options();
#if XE_PLATFORM_WIN32
  if (IsRunningOnWine()) {
    options |= QFileDialog::DontUseNativeDialog;
    options |= QFileDialog::DontResolveSymlinks;
    options |= QFileDialog::DontUseCustomDirectoryIcons;
  }
#endif

  std::vector<std::filesystem::path> selected_files;

  if (multi_selection() && mode() == Mode::kOpen && type() == Type::kFile) {
    // Use getOpenFileNames for multi-selection
    QStringList file_paths = QFileDialog::getOpenFileNames(
        qt_window ? qt_window->qwindow() : nullptr, title, initial_dir, filter,
        nullptr,  // selected filter
        options);

    if (!file_paths.isEmpty()) {
      for (const QString& file_path : file_paths) {
        QByteArray utf8_bytes = file_path.toUtf8();
        std::string file_path_str(utf8_bytes.constData(), utf8_bytes.size());
        selected_files.push_back(xe::to_path(file_path_str));
      }
      set_selected_files(selected_files);
      return true;
    }
  } else {
    // Use getOpenFileName for single selection
    QString file_path = QFileDialog::getOpenFileName(
        qt_window ? qt_window->qwindow() : nullptr, title, initial_dir, filter,
        nullptr,  // selected filter
        options);

    if (!file_path.isEmpty()) {
      QByteArray utf8_bytes = file_path.toUtf8();
      std::string file_path_str(utf8_bytes.constData(), utf8_bytes.size());
      selected_files.push_back(xe::to_path(file_path_str));
      set_selected_files(selected_files);
      return true;
    }
  }

  return false;
}

}  // namespace ui
}  // namespace xe
