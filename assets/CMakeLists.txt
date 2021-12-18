# --------------------------------------------------------------------------------------------------
# Assets cmake file.
# --------------------------------------------------------------------------------------------------

configure_copies(volo_assets_graphics
  graphics/triangle.gra)

configure_copies(volo_assets_meshes
  meshes/cube.obj
  meshes/quad_xy.obj
  meshes/triangle_xy.obj)

configure_shaders(volo_assets_shaders
  shaders/triangle.vert
  shaders/triangle.frag)

configure_copies(volo_assets_textures
  textures/checker.ppm
  textures/circle_glow.tga
  textures/circle.tga
  textures/white.ppm)

if(${EXTERNAL_ASSETS})
  configure_copies_external(volo_assets_external
    bunny.obj
    head/head.obj
    head/lambertian.tga
    corset/corset.obj
    corset/corset_diff.tga
    corset/corset_nrm.tga)
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
