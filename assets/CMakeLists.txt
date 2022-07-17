# --------------------------------------------------------------------------------------------------
# Assets cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> assets")

configure_shaders(volo_shaders
  INCLUDE_DIR shaders/include
  SOURCES
    shaders/debug/debug_skinning.frag
    shaders/debug/debug_skinning.vert
    shaders/debug/grid.frag
    shaders/debug/grid.vert
    shaders/debug/shape_line.vert
    shaders/debug/shape_mesh.vert
    shaders/debug/shape.frag
    shaders/debug/texture_viewer.frag
    shaders/debug/texture_viewer.vert
    shaders/scene/sky_cubemap.frag
    shaders/scene/sky_procedural.frag
    shaders/scene/sky.vert
    shaders/standard_skinned.vert
    shaders/standard.frag
    shaders/standard.vert
    shaders/ui/canvas.frag
    shaders/ui/canvas.vert
    shaders/wireframe_skinned.vert
    shaders/wireframe.frag
    shaders/wireframe.vert
  )

if(${EXTERNAL_ASSETS})
  download_external_files(
    PATH ${CMAKE_CURRENT_SOURCE_DIR}/external
    REMOTE_URL "https://www.bastian.tech/assets"
    FILES
      skybox/sorsele_back.tga
      skybox/sorsele_down.tga
      skybox/sorsele_front.tga
      skybox/sorsele_left.tga
      skybox/sorsele_right.tga
      skybox/sorsele_up.tga
      vanguard/vanguard_diff.tga
      vanguard/vanguard_nrm.tga
      vanguard/vanguard.bin
      vanguard/vanguard.gltf
    )
endif()

add_custom_target(volo_assets)
set_target_properties(volo_assets PROPERTIES path ${CMAKE_CURRENT_SOURCE_DIR})
add_dependencies(volo_assets volo_shaders)
