# --------------------------------------------------------------------------------------------------
# Asset library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: asset")

add_library(lib_asset STATIC
  src/format.c
  src/loader_font_ttf.c
  src/loader_font.c
  src/loader_ftx.c
  src/loader_graphic.c
  src/loader_mesh_obj.c
  src/loader_mesh_pme.c
  src/loader_mesh.c
  src/loader_raw.c
  src/loader_shader_spv.c
  src/loader_shader.c
  src/loader_texture_ppm.c
  src/loader_texture_ptx.c
  src/loader_texture_tga.c
  src/loader_texture.c
  src/loader.c
  src/manager.c
  src/mesh_utils.c
  src/register.c
  src/repo_fs.c
  src/repo_mem.c
  src/repo.c
  )
target_include_directories(lib_asset PUBLIC include)
target_link_libraries(lib_asset PRIVATE lib_data)
target_link_libraries(lib_asset PRIVATE lib_log)
target_link_libraries(lib_asset PUBLIC lib_core)
target_link_libraries(lib_asset PUBLIC lib_ecs)
target_link_libraries(lib_asset PUBLIC lib_geo)

add_executable(lib_asset_test
  test/main.c
  test/test_loader_font_ttf.c
  test/test_loader_ftx.c
  test/test_loader_graphic.c
  test/test_loader_mesh_obj.c
  test/test_loader_raw.c
  test/test_loader_shader_spv.c
  test/test_loader_texture_ppm.c
  test/test_loader_texture_tga.c
  test/test_manager.c
  test/utils.c
  )
target_link_libraries(lib_asset_test PRIVATE lib_asset)
target_link_libraries(lib_asset_test PRIVATE lib_check)
target_link_libraries(lib_asset_test PRIVATE lib_jobs)
target_link_libraries(lib_asset_test PRIVATE lib_log)

configure_test(lib_asset_test SHORT_NAME asset)
configure_debuggable(lib_asset_test)
