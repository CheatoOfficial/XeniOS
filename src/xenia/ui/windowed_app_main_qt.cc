/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <QApplication>
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
