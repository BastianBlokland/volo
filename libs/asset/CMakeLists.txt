# --------------------------------------------------------------------------------------------------
# Asset library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: asset")

add_library(volo_asset STATIC
  src/loader_material.c
  src/loader_mesh_obj.c
  src/loader_mesh.c
  src/loader_raw.c
  src/loader_shader_spv.c
  src/loader_shader.c
  src/loader_texture_ppm.c
  src/loader_texture_tga.c
  src/loader_texture.c
  src/loader.c
  src/manager.c
  src/register.c
  src/repo_fs.c
  src/repo_mem.c
  src/repo.c)
target_include_directories(volo_asset PUBLIC include)
target_link_libraries(volo_asset PUBLIC volo_core)
target_link_libraries(volo_asset PUBLIC volo_ecs)
target_link_libraries(volo_asset PUBLIC volo_geo)
target_link_libraries(volo_asset PRIVATE volo_data)
target_link_libraries(volo_asset PRIVATE volo_log)

add_executable(volo_asset_test
  test/main.c
  test/test_loader_material.c
  test/test_loader_raw.c
  test/test_loader_shader_spv.c
  test/test_loader_texture_ppm.c
  test/test_loader_texture_tga.c
  test/test_manager.c
  test/utils.c)
target_link_libraries(volo_asset_test PRIVATE volo_check)
target_link_libraries(volo_asset_test PRIVATE volo_asset)
target_link_libraries(volo_asset_test PRIVATE volo_jobs)
target_link_libraries(volo_asset_test PRIVATE volo_log)

configure_test(volo_asset_test asset)
configure_debuggable(volo_asset_test)
