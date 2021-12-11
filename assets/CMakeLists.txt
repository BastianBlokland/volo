# --------------------------------------------------------------------------------------------------
# Assets cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> assets")

add_custom_target(volo_assets)

configure_shaders(volo_assets
  shaders/triangle.vert
  shaders/triangle.frag)
