add_library(xenia-ui STATIC)
xe_platform_sources(xenia-ui ${CMAKE_CURRENT_SOURCE_DIR})
# Demo files are standalone executables in XENIA_BUILD_MISC; moc_*.cc are
# owned by AUTOMOC and land in the build tree. Neither should reach this
# target's source list from the repo glob.
file(GLOB _ui_exclude
  "${CMAKE_CURRENT_SOURCE_DIR}/*_demo.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/moc_*.cc"
)
get_target_property(_ui_srcs xenia-ui SOURCES)
if(_ui_exclude AND _ui_srcs)
  list(REMOVE_ITEM _ui_srcs ${_ui_exclude})
  set_target_properties(xenia-ui PROPERTIES SOURCES "${_ui_srcs}")
endif()
# The Qt resource compiler output is checked in under src/xenia/ui/ on
# Linux only (Windows embeds resources via main_resources.rc instead).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ui_resources_qrc.cpp")
  target_sources(xenia-ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui_resources_qrc.cpp)
endif()
target_link_libraries(xenia-ui PUBLIC xenia-base)
if(XE_HAS_QT)
  target_link_libraries(xenia-ui PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
  target_link_libraries(xenia-ui PRIVATE Qt6::GuiPrivate)
  # AUTOMOC-generated moc files sit in xenia-ui_autogen/ and include the
  # original header via `#include "foo.h"`, so their directory needs to
  # be on the quoted-include path. Scoped to this target — the global
  # CMAKE_INCLUDE_CURRENT_DIR is off because other modules have source
  # files that would shadow standard C headers.
  target_include_directories(xenia-ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
if(WIN32)
  target_link_libraries(xenia-ui PRIVATE dwmapi dxgi winmm)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(xenia-ui PRIVATE xcb X11 X11-xcb fontconfig ${GTK3_LIBRARIES})
endif()
xe_target_defaults(xenia-ui)
