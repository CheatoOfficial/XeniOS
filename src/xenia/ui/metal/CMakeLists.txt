add_library(xenia-ui-metal STATIC
  metal_api.cc
  metal_api.h
  metal_gpu_completion_timeline.cc
  metal_gpu_completion_timeline.h
  metal_immediate_drawer.h
  metal_immediate_drawer.mm
  metal_presenter.h
  metal_presenter.mm
  metal_provider.cc
  metal_provider.h
)
target_link_libraries(xenia-ui-metal PUBLIC
  xenia-base
  xenia-ui
  "-framework Foundation"
  "-framework Metal"
  "-framework MetalKit"
  "-framework QuartzCore"
)
if(XE_PLATFORM_IOS)
  target_link_libraries(xenia-ui-metal PUBLIC
    "-framework UIKit"
  )
elseif(XE_PLATFORM_MACOS)
  target_link_libraries(xenia-ui-metal PUBLIC
    "-framework AppKit"
    "-framework MetalFX"
  )
endif()
xe_shader_rules_metal(xenia-ui-metal ${CMAKE_CURRENT_SOURCE_DIR}/../shaders)
if(TARGET xenia-gpu-metal-metal-shaders)
  add_dependencies(xenia-ui-metal xenia-gpu-metal-metal-shaders)
endif()
xe_target_defaults(xenia-ui-metal)
