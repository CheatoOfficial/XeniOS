# Placeholder library for the Metal GPU backend. Exists so the build-time
# Metal shader compilation has a target to hang off; replace placeholder.cc
# with real backend sources when the Metal backend is implemented.
add_library(xenia-gpu-metal STATIC placeholder.cc)
target_link_libraries(xenia-gpu-metal PUBLIC xenia-base xenia-gpu)
xe_shader_rules_metal(xenia-gpu-metal ${CMAKE_CURRENT_SOURCE_DIR}/../shaders)
xe_target_defaults(xenia-gpu-metal)
