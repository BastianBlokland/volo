# --------------------------------------------------------------------------------------------------
# Assets cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> assets")

configure_shaders(volo_shaders
  INCLUDE_DIR shaders/include
  SOURCES
    shaders/scene/grid.vert
    shaders/scene/grid.frag
    shaders/scene/sky.vert
    shaders/scene/sky.frag
    shaders/ui/lines.vert
    shaders/ui/points.vert
    shaders/ui/color.frag
    shaders/ui/text.vert
    shaders/ui/text.frag
    shaders/ui/texture.vert
    shaders/ui/texture.frag
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
    )
endif()

add_custom_target(volo_assets)
set_target_properties(volo_assets PROPERTIES path ${CMAKE_CURRENT_SOURCE_DIR})
add_dependencies(volo_assets volo_shaders)
