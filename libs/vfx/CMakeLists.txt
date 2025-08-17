# --------------------------------------------------------------------------------------------------
# Vfx library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(vfx STATIC
  src/atlas.c
  src/decal.c
  src/register.c
  src/rend.c
  src/sprite.c
  src/stamp.c
  src/stats.c
  src/system.c
  src/warp.c
  )
target_include_directories(vfx PUBLIC include)
target_link_libraries(vfx PUBLIC asset scene core ecs)
target_link_libraries(vfx PRIVATE rend log)

add_executable(vfx_test
  test/config.c
  test/test_warp.c
  )
target_link_libraries(vfx_test PRIVATE app_check vfx)

configure_debuggable(vfx_test)
