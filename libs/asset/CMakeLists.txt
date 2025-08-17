# --------------------------------------------------------------------------------------------------
# Asset library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(asset STATIC
  src/cache.c
  src/data.c
  src/format.c
  src/import_mesh.c
  src/import_texture.c
  src/import.c
  src/loader_decal.c
  src/loader_font_ttf.c
  src/loader_font.c
  src/loader_graphic.c
  src/loader_icon.c
  src/loader_inputmap.c
  src/loader_level.c
  src/loader_mesh_gltf.c
  src/loader_mesh_obj.c
  src/loader_mesh_proc.c
  src/loader_mesh.c
  src/loader_prefab.c
  src/loader_product.c
  src/loader_raw.c
  src/loader_script_scene.c
  src/loader_script.c
  src/loader_shader_glsl.c
  src/loader_shader_spv.c
  src/loader_shader.c
  src/loader_sound_wav.c
  src/loader_sound.c
  src/loader_terrain.c
  src/loader_texture_array.c
  src/loader_texture_atlas.c
  src/loader_texture_font.c
  src/loader_texture_height.c
  src/loader_texture_png.c
  src/loader_texture_ppm.c
  src/loader_texture_proc.c
  src/loader_texture_tga.c
  src/loader_texture.c
  src/loader_vfx.c
  src/loader_weapon.c
  src/loader.c
  src/manager.c
  src/mesh_utils.c
  src/pack.c
  src/property.c
  src/ref.c
  src/register.c
  src/repo_fs.c
  src/repo_mem.c
  src/repo_pack.c
  src/repo.c
  )
target_include_directories(asset PUBLIC include)
target_link_libraries(asset PUBLIC core data ecs geo script)
target_link_libraries(asset PRIVATE json log trace)

add_executable(asset_test
  test/config.c
  test/test_loader_font_ttf.c
  test/test_loader_graphic.c
  test/test_loader_inputmap.c
  test/test_loader_level.c
  test/test_loader_mesh_gltf.c
  test/test_loader_mesh_obj.c
  test/test_loader_prefab.c
  test/test_loader_product.c
  test/test_loader_raw.c
  test/test_loader_script.c
  test/test_loader_shader_spv.c
  test/test_loader_terrain.c
  test/test_loader_texture_atlas.c
  test/test_loader_texture_font.c
  test/test_loader_texture_png.c
  test/test_loader_texture_ppm.c
  test/test_loader_texture_tga.c
  test/test_loader_weapon.c
  test/test_manager.c
  test/utils.c
  )
target_link_libraries(asset_test PRIVATE app_check asset)

configure_debuggable(asset_test)
