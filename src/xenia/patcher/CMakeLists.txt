add_library(xenia-patcher STATIC)
xe_platform_sources(xenia-patcher ${CMAKE_CURRENT_SOURCE_DIR} RECURSIVE)
target_link_libraries(xenia-patcher PUBLIC xenia-base)
xe_target_defaults(xenia-patcher)

set(XE_BUNDLED_PATCHES_DIR
    "${PROJECT_SOURCE_DIR}/build/data_repos/game-patches/patches")
if(NOT IS_DIRECTORY "${XE_BUNDLED_PATCHES_DIR}")
  message(FATAL_ERROR
      "Bundled game patches are missing at ${XE_BUNDLED_PATCHES_DIR}. "
      "Run ./xb fetchdata or ./xb setup before configuring.")
endif()
file(GLOB_RECURSE XE_BUNDLED_PATCH_FILES CONFIGURE_DEPENDS
    "${XE_BUNDLED_PATCHES_DIR}/*.patch.toml")
list(LENGTH XE_BUNDLED_PATCH_FILES XE_BUNDLED_PATCH_FILE_COUNT)
if(XE_BUNDLED_PATCH_FILE_COUNT EQUAL 0)
  message(FATAL_ERROR
      "Bundled game patches directory is empty: ${XE_BUNDLED_PATCHES_DIR}. "
      "Run ./xb fetchdata or ./xb setup before configuring.")
endif()

xe_embed_compressed_bundle(xenia-patcher
    "${XE_BUNDLED_PATCHES_DIR}" patches)
