# --------------------------------------------------------------------------------------------------
# Assets cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> assets")

configure_shaders(volo_shaders
  INCLUDE_DIR shaders/include
  SOURCES
    shaders/debug/grid.frag
    shaders/debug/grid.vert
    shaders/debug/shape_line.vert
    shaders/debug/shape_mesh.vert
    shaders/debug/shape.frag
    shaders/debug/texture_viewer.frag
    shaders/debug/texture_viewer.vert
    shaders/demo/terrain.frag
    shaders/demo/terrain.vert
    shaders/scene/sky_cubemap.frag
    shaders/scene/sky_procedural.frag
    shaders/scene/sky.vert
    shaders/standard.frag
    shaders/standard.vert
    shaders/ui/canvas.frag
    shaders/ui/canvas.vert
    shaders/wireframe.frag
    shaders/wireframe.vert
  )

if(${EXTERNAL_ASSETS})
  download_external_files(
    PATH ${CMAKE_CURRENT_SOURCE_DIR}/external
    REMOTE_URL "https://www.bastian.tech/assets"
    FILES
      boombox/boombox_diff.tga
      boombox/boombox_nrm.tga
      boombox/boombox.bin
      boombox/boombox.gltf
      bunny.obj
      cayo/cayo_diff.tga
      cayo/cayo_nrm.tga
      cayo/cayo.obj
      corset/corset_diff.tga
      corset/corset_gltf.bin
      corset/corset_gltf.gltf
      corset/corset_nrm.tga
      dragon.obj
      head/head.obj
      head/lambertian.tga
      normal_tangent_mirror_test/normal_tangent_mirror_test_diff.tga
      normal_tangent_mirror_test/normal_tangent_mirror_test_nrm.tga
      normal_tangent_mirror_test/normal_tangent_mirror_test.bin
      normal_tangent_mirror_test/normal_tangent_mirror_test.gltf
      normal_tangent_test/normal_tangent_test_diff.tga
      normal_tangent_test/normal_tangent_test_nrm.tga
      normal_tangent_test/normal_tangent_test.bin
      normal_tangent_test/normal_tangent_test.gltf
      pedestal/pedestal_diff.tga
      pedestal/pedestal_nrm.tga
      pedestal/pedestal.obj
      rigged-simple/rigged-simple.bin
      rigged-simple/rigged-simple.gltf
      skybox/sorsele_back.tga
      skybox/sorsele_down.tga
      skybox/sorsele_front.tga
      skybox/sorsele_left.tga
      skybox/sorsele_right.tga
      skybox/sorsele_up.tga
      suzanne/suzanne_diff.tga
      suzanne/suzanne.bin
      suzanne/suzanne.gltf
      terrain/terrain_color.tga
      terrain/terrain_height.r32
      testimages/ducks.tga
    )
endif()

add_custom_target(volo_assets)
set_target_properties(volo_assets PROPERTIES path ${CMAKE_CURRENT_SOURCE_DIR})
add_dependencies(volo_assets volo_shaders)
