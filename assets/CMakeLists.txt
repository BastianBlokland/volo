# --------------------------------------------------------------------------------------------------
# Assets cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> assets")

configure_shaders(volo_shaders
  INCLUDE_DIR shaders/include
  SOURCES
    shaders/debug/grid.vert
    shaders/debug/grid.frag
    shaders/debug/texture_viewer.vert
    shaders/debug/texture_viewer.frag
    shaders/scene/sky.vert
    shaders/scene/sky_cubemap.frag
    shaders/scene/sky_procedural.frag
    shaders/ui/canvas.vert
    shaders/ui/canvas.frag
    shaders/demo/terrain.vert
    shaders/demo/terrain.frag
    shaders/standard.vert
    shaders/standard.frag
  )

if(${EXTERNAL_ASSETS})
  download_external_files(
    PATH ${CMAKE_CURRENT_SOURCE_DIR}/external
    REMOTE_URL "https://www.bastian.tech/assets"
    FILES
      bunny.obj
      head/head.obj
      head/lambertian.tga
      corset/corset.obj
      corset/corset_diff.tga
      corset/corset_nrm.tga
      cayo/cayo.obj
      cayo/cayo_diff.tga
      cayo/cayo_nrm.tga
      pedestal/pedestal.obj
      pedestal/pedestal_diff.tga
      pedestal/pedestal_nrm.tga
      testimages/ducks.tga
      terrain/terrain_color.tga
      terrain/terrain_height.r32
      skybox/sorsele_front.tga
      skybox/sorsele_back.tga
      skybox/sorsele_up.tga
      skybox/sorsele_down.tga
      skybox/sorsele_right.tga
      skybox/sorsele_left.tga
    )
endif()

add_custom_target(volo_assets)
set_target_properties(volo_assets PROPERTIES path ${CMAKE_CURRENT_SOURCE_DIR})
add_dependencies(volo_assets volo_shaders)
