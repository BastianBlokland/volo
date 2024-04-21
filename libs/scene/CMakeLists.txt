# --------------------------------------------------------------------------------------------------
# Scene library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: scene")

add_library(lib_scene STATIC
  src/attachment.c
  src/attack.c
  src/bark.c
  src/bounds.c
  src/camera.c
  src/collision.c
  src/faction.c
  src/footstep.c
  src/health.c
  src/knowledge.c
  src/level.c
  src/lifetime.c
  src/light.c
  src/location.c
  src/locomotion.c
  src/name.c
  src/nav.c
  src/prefab.c
  src/product.c
  src/projectile.c
  src/register.c
  src/renderable.c
  src/script.c
  src/set.c
  src/skeleton.c
  src/sound.c
  src/status.c
  src/tag.c
  src/target.c
  src/terrain.c
  src/time.c
  src/transform.c
  src/vfx.c
  src/visibility.c
  src/weapon.c
  )
target_include_directories(lib_scene PUBLIC include)
target_link_libraries(lib_scene PUBLIC lib_asset lib_core lib_ecs lib_geo)
target_link_libraries(lib_scene PRIVATE lib_log lib_trace)

add_executable(test_lib_scene
  test/config.c
  test/test_nav.c
  test/test_script.c
  test/test_set.c
  )
target_link_libraries(test_lib_scene PRIVATE lib_app_check lib_scene)

configure_test(test_lib_scene SHORT_NAME scene)
configure_debuggable(test_lib_scene)
