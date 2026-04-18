if(WIN32)
  add_executable(xenia-app WIN32)
else()
  add_executable(xenia-app)
endif()

set_target_properties(xenia-app PROPERTIES OUTPUT_NAME "xenia_edge")

target_sources(xenia-app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/xenia_main.cc
)

xe_platform_sources(xenia-app ${CMAKE_CURRENT_SOURCE_DIR})

# Qt owns the windowed main entry point; one implementation is shared
# across platforms.
target_sources(xenia-app PRIVATE
  ${PROJECT_SOURCE_DIR}/src/xenia/ui/windowed_app_main_qt.cc
)

if(WIN32)
  target_sources(xenia-app PRIVATE
    ${PROJECT_SOURCE_DIR}/src/xenia/base/main_init_win.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/main_resources.rc
    ${PROJECT_SOURCE_DIR}/src/xenia/base/app_win32.manifest
  )
  if(XE_TARGET_X86_64)
    # main_init runs the AVX feature check — it must itself not use AVX.
    set_source_files_properties(
      ${PROJECT_SOURCE_DIR}/src/xenia/base/main_init_win.cc
      PROPERTIES COMPILE_OPTIONS "/arch:SSE2"
    )
  endif()
  # WIN32 executable defaults to /SUBSYSTEM:WINDOWS; override to CONSOLE
  # in Debug so stdout/stderr reach an attached terminal. /ENTRY points
  # at the Qt-style main() rather than WinMain.
  target_link_options(xenia-app PRIVATE
    $<$<CONFIG:Debug>:/SUBSYSTEM:CONSOLE>
    /ENTRY:mainCRTStartup
  )
endif()

target_compile_definitions(xenia-app PRIVATE
  XBYAK_NO_OP_NAMES
  XBYAK_ENABLE_OMITTED_OPERAND
)

target_link_libraries(xenia-app PRIVATE
  xenia-apu
  xenia-apu-nop
  xenia-base
  xenia-core
  xenia-cpu
  xenia-gpu
  xenia-gpu-null
  xenia-hid
  xenia-hid-nop
  xenia-kernel
  xenia-patcher
  xenia-ui
  xenia-vfs
  aes_128
  capstone
  fmt
  dxbc
  discord-rpc
  imgui
  libavcodec
  libavutil
  mspack
  snappy
  xxhash
  xenia-app-discord
  xenia-apu-sdl
  xenia-debug-ui
  xenia-helper-sdl
  xenia-hid-sdl
)

# Vulkan/SPIR-V backends (not macOS)
if(NOT APPLE)
  target_link_libraries(xenia-app PRIVATE
    xenia-gpu-vulkan
    xenia-ui-vulkan
    glslang-spirv
  )
endif()

# Architecture-specific backend
if(XE_TARGET_X86_64)
  target_link_libraries(xenia-app PRIVATE xenia-cpu-backend-x64)
elseif(XE_TARGET_AARCH64)
  target_link_libraries(xenia-app PRIVATE xenia-cpu-backend-a64)
endif()

if(WIN32)
  target_link_libraries(xenia-app PRIVATE
    xenia-apu-xaudio2
    xenia-debug-gdb
    xenia-gpu-d3d12
    xenia-hid-winkey
    xenia-hid-xinput
    xenia-ui-d3d12
  )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(xenia-app PRIVATE
    xenia-apu-alsa
    X11
    xcb
    X11-xcb
    SDL2
  )
endif()

xe_target_defaults(xenia-app)

# ==============================================================================
# Post-build: Qt deployment and runtime asset copy
# ==============================================================================

set(_edge_assets_font_src "${PROJECT_SOURCE_DIR}/assets/font")
set(_edge_optimized_src "${PROJECT_SOURCE_DIR}/.data_repos/optimized-settings/settings")
set(_edge_game_patches_src "${PROJECT_SOURCE_DIR}/.data_repos/game-patches/patches")

# windeployqt always deploys --release DLLs. All our configs link
# release Qt (root CMakeLists maps Debug/Checked/Valgrind imported
# configs to Release), so deploying Qt6Cored.dll would pull in the
# debug CRT and corrupt Qt/STL objects at runtime.
if(WIN32 AND TARGET Qt6::windeployqt)
  set(_windeployqt_args
    --release
    --no-translations
    --no-system-d3d-compiler
    --no-opengl-sw
    --no-compiler-runtime)
  # Cross-compile: Qt6::windeployqt is imported from the host Qt via
  # QT_HOST_PATH and self-locates its Qt from its own bin path — so without
  # --qtpaths it deploys host-arch DLLs next to a target-arch exe and the
  # loader fails with 0xc000007b on the target. Point it at the target Qt's
  # qtpaths so it deploys target-arch DLLs instead.
  #   x64 host → ARM64 target: target Qt is the "cross_compiled" flavor and
  #     ships a host-runnable .bat shim (host-qtpaths.bat on Qt 6.10.1+,
  #     qtpaths.bat on older versions).
  #   ARM64 host → x64 target: target Qt is the normal x64 desktop build;
  #     its qtpaths.exe runs natively under Prism emulation on ARM64 Windows.
  if(CMAKE_CROSSCOMPILING AND XE_QT_DIR)
    if(EXISTS "${XE_QT_DIR}/bin/host-qtpaths.bat")
      list(APPEND _windeployqt_args --qtpaths "${XE_QT_DIR}/bin/host-qtpaths.bat")
    elseif(EXISTS "${XE_QT_DIR}/bin/qtpaths.bat")
      list(APPEND _windeployqt_args --qtpaths "${XE_QT_DIR}/bin/qtpaths.bat")
    elseif(EXISTS "${XE_QT_DIR}/bin/qtpaths.exe")
      list(APPEND _windeployqt_args --qtpaths "${XE_QT_DIR}/bin/qtpaths.exe")
    endif()
  endif()
  add_custom_command(TARGET xenia-app POST_BUILD
    COMMAND Qt6::windeployqt ${_windeployqt_args} "$<TARGET_FILE:xenia-app>"
    # icuuc.dll is a Windows 10+ system forwarder (C:\Windows\System32\icuuc.dll)
    # that windeployqt wrongly deploys from the host's System32 — host-arch on a
    # cross-compile, shadowing the target's own system copy. Upstream fix tracked
    # in QTBUG-142131, targeted for Qt 6.11.1.
    COMMAND ${CMAKE_COMMAND} -E remove -f "$<TARGET_FILE_DIR:xenia-app>/icuuc.dll"
    VERBATIM
  )
endif()

# Stage runtime assets (fonts, game patches, optimized settings) next to
# the executable. Same layout on Windows and Linux.
add_custom_command(TARGET xenia-app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:xenia-app>/optimized_settings"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:xenia-app>/game_patches"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:xenia-app>/assets/font"
  VERBATIM
)
if(EXISTS "${_edge_optimized_src}")
  add_custom_command(TARGET xenia-app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${_edge_optimized_src}" "$<TARGET_FILE_DIR:xenia-app>/optimized_settings"
    VERBATIM
  )
endif()
if(EXISTS "${_edge_game_patches_src}")
  add_custom_command(TARGET xenia-app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${_edge_game_patches_src}" "$<TARGET_FILE_DIR:xenia-app>/game_patches"
    VERBATIM
  )
endif()
if(EXISTS "${_edge_assets_font_src}")
  add_custom_command(TARGET xenia-app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${_edge_assets_font_src}" "$<TARGET_FILE_DIR:xenia-app>/assets/font"
    VERBATIM
  )
endif()
