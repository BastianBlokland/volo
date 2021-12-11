# --------------------------------------------------------------------------------------------------
# Assets cmake file.
# --------------------------------------------------------------------------------------------------

configure_copies(volo_assets_graphics
  graphics/triangle.gfx)

configure_copies(volo_assets_meshes
  meshes/cube.obj
  meshes/quad_xy.obj
  meshes/triangle.obj)

configure_shaders(volo_assets_shaders
  shaders/triangle.vert
  shaders/triangle.frag)

configure_copies(volo_assets_textures
  textures/checker.ppm
  textures/circle_glow.tga
  textures/circle.tga
  textures/white.ppm)

add_custom_target(volo_assets)
add_dependencies(volo_assets volo_assets_graphics)
add_dependencies(volo_assets volo_assets_meshes)
add_dependencies(volo_assets volo_assets_shaders)
add_dependencies(volo_assets volo_assets_textures)
