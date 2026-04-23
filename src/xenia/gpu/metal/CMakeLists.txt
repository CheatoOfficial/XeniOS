add_library(xenia-gpu-metal STATIC
  metal_command_processor.cc
  metal_command_processor.h
  metal_graphics_system.cc
  metal_graphics_system.h
  metal_heap_pool.cc
  metal_heap_pool.h
  metal_primitive_processor.cc
  metal_primitive_processor.h
  metal_shared_memory.cc
  metal_shared_memory.h
  metal_upload_buffer_pool.cc
  metal_upload_buffer_pool.h
)
target_link_libraries(xenia-gpu-metal PUBLIC
  xenia-base
  xenia-gpu
  xenia-ui-metal
  "-framework Foundation"
  "-framework Metal"
  "-framework MetalKit"
)
xe_shader_rules_metal(xenia-gpu-metal ${CMAKE_CURRENT_SOURCE_DIR}/../shaders)
xe_target_defaults(xenia-gpu-metal)
