/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_QT_UTIL_H_
#define XENIA_UI_QT_UTIL_H_

#include <QString>
#include <string>

namespace xe {
namespace ui {

// Safely convert std::string to QString, handling invalid UTF-8 data
// Uses QString::fromUtf8 which is more robust than fromStdString
inline QString SafeQString(const std::string& str) {
  return QString::fromUtf8(str.c_str(), static_cast<qsizetype>(str.size()));
}

// Safely convert QString to std::string, handling invalid UTF-8 data
// Uses QString::toUtf8 which is more robust than toStdString
inline std::string SafeStdString(const QString& qstr) {
  QByteArray utf8 = qstr.toUtf8();
  return std::string(utf8.constData(), utf8.size());
}

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_QT_UTIL_H_
