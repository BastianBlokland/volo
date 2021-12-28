# --------------------------------------------------------------------------------------------------
# Assets cmake file.
# --------------------------------------------------------------------------------------------------

configure_copies(volo_assets_graphics
  graphics/cube.gra
  graphics/demo_head.gra
  graphics/demo_corset.gra
  graphics/sky.gra)

configure_copies(volo_assets_meshes
  meshes/cube.obj
  meshes/missing.obj
  meshes/quad_xy.obj
  meshes/triangle_xy.obj)

configure_shaders(volo_assets_shaders
  shaders/sky.vert
  shaders/sky.frag
  shaders/standard.vert
  shaders/standard.frag)

configure_copies(volo_assets_textures
  textures/checker.ppm
  textures/circle_glow.tga
  textures/circle.tga
  textures/missing.ppm
  textures/white.ppm)

if(${EXTERNAL_ASSETS})
  configure_copies_external(volo_assets_external
    bunny.obj
    head/head.obj
    head/lambertian.tga
    corset/corset.obj
    corset/corset_diff.tga
    corset/corset_nrm.tga
    testimages/ducks.tga)
endif()

add_custom_target(volo_assets)
set_target_properties(volo_assets PROPERTIES path ${CMAKE_CURRENT_BINARY_DIR})

add_dependencies(volo_assets volo_assets_graphics)
add_dependencies(volo_assets volo_assets_meshes)
add_dependencies(volo_assets volo_assets_shaders)
add_dependencies(volo_assets volo_assets_textures)
if(${EXTERNAL_ASSETS})
  add_dependencies(volo_assets volo_assets_external)
endif()
