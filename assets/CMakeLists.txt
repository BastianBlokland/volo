# --------------------------------------------------------------------------------------------------
# Assets cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> assets")

add_custom_target(volo_assets)

configure_shaders(volo_assets
  shaders/triangle.vert
  shaders/triangle.frag)

  add_custom_target(copy-runtime-files ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/runtime-files-dir ${CMAKE_BINARY_DIR}/runtime-files-dir
    DEPENDS ${MY_TARGET})
