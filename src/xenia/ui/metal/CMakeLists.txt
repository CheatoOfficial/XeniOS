# Placeholder library for the Metal UI backend. Exists so the build-time
# Metal shader compilation has a target to hang off; replace placeholder.cc
# with real backend sources when the Metal backend is implemented.
add_library(xenia-ui-metal STATIC placeholder.cc)
target_link_libraries(xenia-ui-metal PUBLIC xenia-base xenia-ui)
xe_shader_rules_metal(xenia-ui-metal ${CMAKE_CURRENT_SOURCE_DIR}/../shaders)
xe_target_defaults(xenia-ui-metal)
