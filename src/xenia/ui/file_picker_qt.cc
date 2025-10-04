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
#include "xenia/ui/window_qt.h"

namespace xe {
namespace ui {

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
  QString title;
  QFileDialog::FileMode file_mode;
  QFileDialog::AcceptMode accept_mode;

  switch (mode()) {
    case Mode::kOpen:
      if (type() == Type::kFile) {
        title = "Open File";
        file_mode = QFileDialog::ExistingFile;
      } else {
        title = "Open Directory";
        file_mode = QFileDialog::Directory;
      }
      accept_mode = QFileDialog::AcceptOpen;
      break;
    case Mode::kSave:
      title = "Save File";
      file_mode = QFileDialog::AnyFile;
      accept_mode = QFileDialog::AcceptSave;
      break;
    default:
      XELOGE("QtFilePicker::Show: Unhandled mode: {}, Type: {}",
             static_cast<int>(mode()), static_cast<int>(type()));
      assert_always();
      return false;
  }

  auto* qt_window =
      parent_window ? dynamic_cast<QtWindow*>(parent_window) : nullptr;

  // Use static QFileDialog function to avoid Qt container ABI issues on Windows
  QString initial_dir;
  if (!initial_directory().empty()) {
    std::string dir_str = initial_directory().string();
    initial_dir =
        QString::fromUtf8(dir_str.c_str(), static_cast<int>(dir_str.size()));
  }

  // Try native dialog; Qt will automatically fall back to its own dialog if
  // native is unavailable
  QString file_path = QFileDialog::getOpenFileName(
      qt_window ? qt_window->qwindow() : nullptr, title, initial_dir,
      QString(),  // filter
      nullptr     // selected filter
  );

  if (!file_path.isEmpty()) {
    QByteArray utf8_bytes = file_path.toUtf8();
    std::string file_path_str(utf8_bytes.constData(), utf8_bytes.size());

    std::vector<std::filesystem::path> selected_files;
    selected_files.push_back(xe::to_path(file_path_str));
    set_selected_files(selected_files);
    return true;
  }

  return false;
}

}  // namespace ui
}  // namespace xe
