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

# Per-platform windowed entry point.
if(WIN32)
  target_sources(xenia-app PRIVATE
    ${PROJECT_SOURCE_DIR}/src/xenia/ui/windowed_app_main_win.cc
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
  # in Debug so stdout/stderr reach an attached terminal. Pin the entry
  # explicitly: wxIMPLEMENT_APP_NO_MAIN only emits wWinMain, so without
  # /ENTRY the CONSOLE subsystem looks for a missing main().
  target_link_options(xenia-app PRIVATE
    $<$<CONFIG:Debug>:/SUBSYSTEM:CONSOLE>
    /ENTRY:wWinMainCRTStartup
  )
elseif(APPLE)
  target_sources(xenia-app PRIVATE
    ${PROJECT_SOURCE_DIR}/src/xenia/ui/windowed_app_main_mac.cc
  )
else()
  target_sources(xenia-app PRIVATE
    ${PROJECT_SOURCE_DIR}/src/xenia/ui/windowed_app_main_linux.cc
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
    xenia-hid-keyboard
    X11
    xcb
    X11-xcb
    SDL2
  )
elseif(APPLE)
  target_link_libraries(xenia-app PRIVATE
    xenia-gpu-metal
    xenia-hid-keyboard
    xenia-ui-metal
  )
endif()

xe_target_defaults(xenia-app)

# Bake icons into the app binary (font lives with xenia-ui).
xe_embed_binary_assets(xenia-app
    "${PROJECT_SOURCE_DIR}/assets/icons" icons)

# Bake game-compat data (canary.json from xenia-manager/database).
xe_embed_compressed_bundle(xenia-app
    "${PROJECT_SOURCE_DIR}/build/data_repos/xenia-manager-database/data/game-compatibility"
    game_compat)
target_include_directories(xenia-app PRIVATE
    "${PROJECT_SOURCE_DIR}/third_party/rapidjson/include")
