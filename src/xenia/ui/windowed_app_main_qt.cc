/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <cstdio>
#include <cstdlib>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/ui/windowed_app.h"
#include "xenia/ui/windowed_app_context_qt.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/main_win.h"
#include "xenia/base/platform_win.h"
#endif

int main(int argc, char** argv) {
#if XE_PLATFORM_LINUX
  // Force X11 backend (Vulkan needs XCB, not Wayland)
  qputenv("QT_QPA_PLATFORM", "xcb");
#endif

  QApplication qt_app(argc, argv);

  // Force dark theme
  QPalette dark_palette;
  dark_palette.setColor(QPalette::Window, QColor(53, 53, 53));
  dark_palette.setColor(QPalette::WindowText, Qt::white);
  dark_palette.setColor(QPalette::Base, QColor(35, 35, 35));
  dark_palette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
  dark_palette.setColor(QPalette::ToolTipBase, QColor(25, 25, 25));
  dark_palette.setColor(QPalette::ToolTipText, Qt::white);
  dark_palette.setColor(QPalette::Text, Qt::white);
  dark_palette.setColor(QPalette::Button, QColor(53, 53, 53));
  dark_palette.setColor(QPalette::ButtonText, Qt::white);
  dark_palette.setColor(QPalette::BrightText, Qt::red);
  dark_palette.setColor(QPalette::Link, QColor(42, 130, 218));
  dark_palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
  dark_palette.setColor(QPalette::HighlightedText, Qt::black);
  dark_palette.setColor(QPalette::Disabled, QPalette::Text,
                        QColor(127, 127, 127));
  dark_palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                        QColor(127, 127, 127));
  qt_app.setPalette(dark_palette);

  // Fix menu spacing and styling
  qt_app.setStyleSheet(
      "QMenuBar::item { "
      "  padding: 5px 10px; "
      "} "
      "QMenu { "
      "  background-color: rgb(53, 53, 53); "
      "  border: 1px solid rgb(80, 80, 80); "
      "  border-radius: 0px; "
      "} "
      "QMenu::item { "
      "  padding: 5px 25px 5px 10px; "
      "  background-color: transparent; "
      "} "
      "QMenu::item:selected { "
      "  background-color: rgb(42, 130, 218); "
      "  color: black; "
      "} "
      "QMenu::indicator { width: 0px; margin-left: 0px; }");

  // Set different application name for game processes so they show as separate
  // dock entries. Check if we have a target file argument (game process) or not
  // (UI process).
  bool is_game_process = argc > 1;
  if (is_game_process) {
    qt_app.setApplicationName("Xbox 360 Game");
    qt_app.setDesktopFileName("xenia-game");
  } else {
    qt_app.setApplicationName("Xenia Edge");
    qt_app.setDesktopFileName("xenia-edge");
  }

  // Use Qt's own menu bar instead of native on all platforms
  qt_app.setAttribute(Qt::AA_DontUseNativeMenuBar);

  int result;

  {
    xe::ui::QtWindowedAppContext app_context(&qt_app);

    std::unique_ptr<xe::ui::WindowedApp> app =
        xe::ui::GetWindowedAppCreator()(app_context);

    cvar::ParseLaunchArguments(argc, argv, app->GetPositionalOptionsUsage(),
                               app->GetPositionalOptions());

#if XE_PLATFORM_WIN32
    // Initialize COM for Windows
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
      return EXIT_FAILURE;
    }

    // Use Windows-specific initialization which properly sets up logging
    xe::InitializeWin32App(app->GetName());
#else
    xe::InitializeLogging(app->GetName());
#endif

    if (app->OnInitialize()) {
      app_context.RunMainQtLoop();
      result = EXIT_SUCCESS;
    } else {
      result = EXIT_FAILURE;
    }

    app->InvokeOnDestroy();
  }

#if XE_PLATFORM_WIN32
  xe::ShutdownWin32App();
  CoUninitialize();
#else
  xe::ShutdownLogging();
#endif

  return result;
}
