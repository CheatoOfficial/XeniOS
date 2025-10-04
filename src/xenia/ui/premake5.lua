project_root = "../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-ui")
  uuid("d0407c25-b0ea-40dc-846c-82c46fbd9fa2")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
  })
  local_platform_files()
  removefiles({
    "*_demo.cc",
    -- Exclude old platform-specific implementations in favor of Qt
    "window_gtk.cc",
    "window_win.cc",
    "file_picker_gtk.cc",
    "file_picker_win.cc",
    "windowed_app_context_gtk.cc",
    "windowed_app_context_win.cc",
    "windowed_app_main_posix.cc",
    "windowed_app_main_win.cc",
  })
  if os.istarget("android") then
    filter("platforms:Android-*")
      -- Exports JNI functions.
      wholelib("On")
  end

  filter("platforms:Windows")
    links({
      "dwmapi",
      "dxgi",
      "winmm",
    })

  filter("platforms:Linux")
    links({
      "xcb",
      "X11",
      "X11-xcb",
      "fontconfig"
    })
