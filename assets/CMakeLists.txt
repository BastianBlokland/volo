# --------------------------------------------------------------------------------------------------
# Assets cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> assets")

configure_shaders(volo_shaders
  INCLUDE_DIR shaders/include
  SOURCES
    shaders/compose.frag
    shaders/debug/debug_skinning.frag
    shaders/debug/debug_skinning.vert
    shaders/debug/grid.frag
    shaders/debug/grid.vert
    shaders/debug/shape_line.vert
    shaders/debug/shape_mesh.vert
    shaders/debug/shape.frag
    shaders/debug/texture_viewer.frag
    shaders/debug/texture_viewer.vert
    shaders/depth_only.frag
    shaders/fullscreen.vert
    shaders/light/light_directional.frag
    shaders/light/light_directional.vert
    shaders/light/light_point_debug.frag
    shaders/light/light_point.frag
    shaders/light/light_point.vert
    shaders/scene/sky_cubemap.frag
    shaders/scene/sky_procedural.frag
    shaders/scene/sky.vert
    shaders/scene/terrain_position.vert
    shaders/scene/terrain.frag
    shaders/scene/terrain.vert
    shaders/standard_position.vert
    shaders/standard_skinned_position.vert
    shaders/standard_skinned.vert
    shaders/standard.frag
    shaders/standard.vert
    shaders/ui/canvas.frag
    shaders/ui/canvas.vert
    shaders/vfx/particle.frag
    shaders/vfx/particle.vert
    shaders/white.frag
  )

if(${EXTERNAL_ASSETS})
  download_external_files(
    PATH ${CMAKE_CURRENT_SOURCE_DIR}/external
    REMOTE_URL "https://www.bastian.tech/assets"
    FILES
      fence/fence_1_color_rough.tga
      fence/fence_1_normal.tga
      fence/fence_1.bin
      fence/fence_1.gltf
      fire/fire_0.tga
      fire/fire_1.tga
      fire/fire_10.tga
      fire/fire_11.tga
      fire/fire_12.tga
      fire/fire_13.tga
      fire/fire_14.tga
      fire/fire_15.tga
      fire/fire_2.tga
      fire/fire_3.tga
      fire/fire_4.tga
      fire/fire_5.tga
      fire/fire_6.tga
      fire/fire_7.tga
      fire/fire_8.tga
      fire/fire_9.tga
      maynard/maynard_color_rough.tga
      maynard/maynard_nrm.tga
      maynard/maynard.bin
      maynard/maynard.gltf
      skybox/teide_back.tga
      skybox/teide_down.tga
      skybox/teide_forward.tga
      skybox/teide_left.tga
      skybox/teide_right.tga
      skybox/teide_up.tga
      swat/swat_3.bin
      swat/swat_3.gltf
      swat/swat_color_rough.tga
      swat/swat_nrm.tga
      terrain/terrain_3_height.r16
      terrain/terrain_3_splat.tga
      tiling/dirt_2_color_rough.tga
      tiling/dirt_2_normal.tga
      tiling/sand_1_color_rough.tga
      tiling/sand_1_normal.tga
      turret/turret_big.gltf
      turret/turret_color_rough.tga
      turret/turret_missile.bin
      turret/turret_missile.gltf
      turret/turret_nrm.tga
      turret/turret.bin
      vfx/flash_star.tga
      vfx/flash.tga
      vfx/rock.tga
      vfx/smoke.tga
      vfx/trail.tga
    )
endif()

add_custom_target(volo_assets)
set_target_properties(volo_assets PROPERTIES path ${CMAKE_CURRENT_SOURCE_DIR})
add_dependencies(volo_assets volo_shaders)
