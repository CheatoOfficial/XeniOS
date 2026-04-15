add_library(xenia-ui-vulkan STATIC)
xe_platform_sources(xenia-ui-vulkan ${CMAKE_CURRENT_SOURCE_DIR})
xe_platform_sources(xenia-ui-vulkan ${CMAKE_CURRENT_SOURCE_DIR}/functions)
xe_shader_rules_spirv(xenia-ui-vulkan ${CMAKE_CURRENT_SOURCE_DIR}/../shaders)
target_include_directories(xenia-ui-vulkan PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/Vulkan-Headers/include
  ${PROJECT_SOURCE_DIR}/third_party/glslang
)
target_link_libraries(xenia-ui-vulkan PUBLIC xenia-base xenia-ui)

# SPIRV-Tools: Vulkan SDK on Windows (headers + libs under $VULKAN_SDK),
# system package on Linux (libspirv-tools-*).
if(WIN32 AND DEFINED ENV{VULKAN_SDK})
  target_include_directories(xenia-ui-vulkan PRIVATE "$ENV{VULKAN_SDK}/Include")
  target_link_directories(xenia-ui-vulkan PUBLIC "$ENV{VULKAN_SDK}/Lib")
  target_link_libraries(xenia-ui-vulkan PUBLIC SPIRV-Tools-opt SPIRV-Tools)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(xenia-ui-vulkan PUBLIC SPIRV-Tools-opt SPIRV-Tools)
endif()
xe_target_defaults(xenia-ui-vulkan)

if(XENIA_BUILD_MISC)
  add_executable(xenia-ui-window-vulkan-demo
    ${CMAKE_CURRENT_SOURCE_DIR}/../window_demo.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/vulkan_window_demo.cc
    ${PROJECT_SOURCE_DIR}/src/xenia/ui/windowed_app_main_qt.cc
  )
  if(WIN32)
    set_target_properties(xenia-ui-window-vulkan-demo PROPERTIES WIN32_EXECUTABLE TRUE)
  endif()
  target_include_directories(xenia-ui-window-vulkan-demo PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/Vulkan-Headers/include
  )
  target_link_libraries(xenia-ui-window-vulkan-demo PRIVATE
    xenia-apu xenia-base xenia-core xenia-cpu
    xenia-gpu xenia-hid xenia-hid-portal
    xenia-kernel xenia-patcher xenia-ui xenia-ui-vulkan xenia-vfs
    aes_128 capstone dxbc fmt glslang-spirv imgui mspack snappy xxhash
  )
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_link_libraries(xenia-ui-window-vulkan-demo PRIVATE xenia-cpu-backend-x64)
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(xenia-ui-window-vulkan-demo PRIVATE X11 xcb X11-xcb xenia-apu-sdl)
  endif()
  xe_target_defaults(xenia-ui-window-vulkan-demo)
endif()
