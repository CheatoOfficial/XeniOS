xe_test_suite(xenia-cpu-tests ${CMAKE_CURRENT_SOURCE_DIR}
  LINKS
    capstone fmt imgui
    xenia-apu xenia-base xenia-core xenia-cpu xenia-gpu
    xenia-hid-skylander xenia-kernel xenia-ui xenia-patcher
)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND TARGET xenia-cpu-tests)
  target_link_libraries(xenia-cpu-tests PRIVATE xenia-cpu-backend-x64)
endif()
# xenia-apu pulls in xenia-apu-sdl on Linux (see src/xenia/apu/CMakeLists.txt).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET xenia-cpu-tests)
  target_link_libraries(xenia-cpu-tests PRIVATE xenia-apu-sdl)
endif()
