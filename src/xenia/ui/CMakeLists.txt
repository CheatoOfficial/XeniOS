add_library(xenia-ui STATIC)
xe_platform_sources(xenia-ui ${CMAKE_CURRENT_SOURCE_DIR})
# Demo files are standalone executables in XENIA_BUILD_MISC; windowed_app_main_*
# files are added explicitly per-target. wx files are excluded on Android (no
# xe-wxWidgets target there).
file(GLOB _ui_exclude
  "${CMAKE_CURRENT_SOURCE_DIR}/*_demo.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/windowed_app_main_*.cc"
)
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
  file(GLOB _wx_exclude
    "${CMAKE_CURRENT_SOURCE_DIR}/*_wx.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*_wx.h"
  )
  list(APPEND _ui_exclude ${_wx_exclude})
endif()
get_target_property(_ui_srcs xenia-ui SOURCES)
if(_ui_exclude AND _ui_srcs)
  list(REMOVE_ITEM _ui_srcs ${_ui_exclude})
  set_target_properties(xenia-ui PROPERTIES SOURCES "${_ui_srcs}")
endif()
target_link_libraries(xenia-ui PUBLIC xenia-base)
if(XE_HAS_QT)
  target_link_libraries(xenia-ui PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
  target_link_libraries(xenia-ui PRIVATE Qt6::GuiPrivate)
  set_target_properties(xenia-ui PROPERTIES AUTOMOC ON AUTORCC ON)
  # Qt resources: AUTORCC picks up .qrc files listed in target_sources,
  # generates qrc_<stem>.cpp into xenia-ui_autogen/, and compiles it in.
  # Windows embeds the same assets via main_resources.rc instead.
  if(NOT WIN32)
    target_sources(xenia-ui PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/ui_resources.qrc)
  endif()
  # AUTOMOC-generated moc files sit in xenia-ui_autogen/ and include the
  # original header via `#include "foo.h"`, so their directory needs to
  # be on the quoted-include path. Scoped to this target — the global
  # CMAKE_INCLUDE_CURRENT_DIR is off because other modules have source
  # files that would shadow standard C headers.
  target_include_directories(xenia-ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
if(WIN32)
  target_link_libraries(xenia-ui PRIVATE dxgi)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # xcb / X11 / X11-xcb: Vulkan surface creation in window_wx.cc.
  # fontconfig: imgui_drawer.cc font lookup.
  # GTK3: XCB handle extraction in window_wx.cc (gtk_widget_realize, etc.).
  target_link_libraries(xenia-ui PRIVATE xcb X11 X11-xcb fontconfig ${GTK3_LIBRARIES})
elseif(APPLE)
  # xe_platform_sources globs *.cc only; surface_mac.mm is Objective-C++.
  target_sources(xenia-ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/surface_mac.mm)
  target_link_libraries(xenia-ui PRIVATE
    "-framework Cocoa"
    "-framework QuartzCore"
  )
endif()
xe_target_defaults(xenia-ui)

xe_embed_binary_assets(xenia-ui
    "${PROJECT_SOURCE_DIR}/assets/font" font)
xe_embed_binary_assets(xenia-ui
    "${PROJECT_SOURCE_DIR}/assets/icon" app_icon)
