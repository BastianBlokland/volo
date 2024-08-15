# --------------------------------------------------------------------------------------------------
# Vfx library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: vfx")

add_library(lib_vfx STATIC
  src/atlas.c
  src/decal.c
  src/draw.c
  src/register.c
  src/sprite.c
  src/stamp.c
  src/stats.c
  src/system.c
  src/warp.c
  )
target_include_directories(lib_vfx PUBLIC include)
target_link_libraries(lib_vfx PUBLIC lib_asset lib_scene lib_core lib_ecs)
target_link_libraries(lib_vfx PRIVATE lib_rend lib_log)

add_executable(test_lib_vfx
  test/config.c
  test/test_warp.c
  )
target_link_libraries(test_lib_vfx PRIVATE lib_app_check lib_vfx)

configure_test(test_lib_vfx SHORT_NAME vfx)
configure_debuggable(test_lib_vfx)
