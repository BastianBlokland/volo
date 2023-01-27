# --------------------------------------------------------------------------------------------------
# Assets cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> assets")

configure_shaders(volo_shaders
  INCLUDE_DIR shaders/include
  SOURCES
    shaders/ambient_occlusion.frag
    shaders/ambient.frag
    shaders/bloom_down.frag
    shaders/bloom_up.frag
    shaders/debug/dyn_image_viewer.frag
    shaders/debug/dyn_image_viewer.vert
    shaders/debug/dyn_mesh_viewer_skinned.vert
    shaders/debug/dyn_mesh_viewer.vert
    shaders/debug/grid.frag
    shaders/debug/grid.vert
    shaders/debug/shape_line.vert
    shaders/debug/shape_mesh.vert
    shaders/debug/shape.frag
    shaders/debug/skinning.frag
    shaders/debug/skinning.vert
    shaders/dyn_depth_clip.frag
    shaders/dyn_depth_standard_skinned.vert
    shaders/dyn_depth_standard.vert
    shaders/dyn_depth.frag
    shaders/fullscreen.vert
    shaders/light/light_directional.frag
    shaders/light/light_directional.vert
    shaders/light/light_point_debug.frag
    shaders/light/light_point.frag
    shaders/light/light_point.vert
    shaders/outline.frag
    shaders/scene/sky_cubemap.frag
    shaders/scene/sky_procedural.frag
    shaders/scene/sky.vert
    shaders/scene/terrain_position.vert
    shaders/scene/terrain.frag
    shaders/scene/terrain.vert
    shaders/standard_skinned.vert
    shaders/standard.frag
    shaders/standard.vert
    shaders/tonemapper.frag
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
      barrel/barrel_color_rough.tga
      barrel/barrel_normal.tga
      barrel/barrel.bin
      barrel/barrel.gltf
      fence/fence_1_color_rough.tga
      fence/fence_1_normal.tga
      fence/fence_1.bin
      fence/fence_1.gltf
      fire_2/fire_0.tga
      fire_2/fire_1.tga
      fire_2/fire_10.tga
      fire_2/fire_11.tga
      fire_2/fire_12.tga
      fire_2/fire_13.tga
      fire_2/fire_14.tga
      fire_2/fire_15.tga
      fire_2/fire_2.tga
      fire_2/fire_3.tga
      fire_2/fire_4.tga
      fire_2/fire_5.tga
      fire_2/fire_6.tga
      fire_2/fire_7.tga
      fire_2/fire_8.tga
      fire_2/fire_9.tga
      maynard/maynard_color_rough.tga
      maynard/maynard_nrm.tga
      maynard/maynard.bin
      maynard/maynard.gltf
      skybox/teide_small_back.tga
      skybox/teide_small_down.tga
      skybox/teide_small_forward.tga
      skybox/teide_small_left.tga
      skybox/teide_small_right.tga
      skybox/teide_small_up.tga
      swat/swat_3.bin
      swat/swat_3.gltf
      swat/swat_color_rough.tga
      swat/swat_nrm.tga
      terrain/terrain_3_height.r16
      terrain/terrain_3_splat.tga
      tiling/clouds.tga
      tiling/dirt_2_color_rough.tga
      tiling/dirt_2_normal.tga
      tiling/sand_1_color_rough.tga
      tiling/sand_1_normal.tga
      tree_acacia/tree_acacia_alpha.tga
      tree_acacia/tree_acacia_2_color_rough.tga
      tree_acacia/tree_acacia_normal.tga
      tree_acacia/tree_acacia.bin
      tree_acacia/tree_acacia.gltf
      turret/turret_big.gltf
      turret/turret_color_rough.tga
      turret/turret_missile.bin
      turret/turret_missile.gltf
      turret/turret_nrm.tga
      turret/turret.bin
      vfx/flash_2.tga
      vfx/flash_star_2.tga
      vfx/rock.tga
      vfx/smoke.tga
      vfx/trail_2.tga
    )
endif()

add_custom_target(volo_assets)
set_target_properties(volo_assets PROPERTIES path ${CMAKE_CURRENT_SOURCE_DIR})
add_dependencies(volo_assets volo_shaders)
