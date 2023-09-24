# --------------------------------------------------------------------------------------------------
# Scene library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: scene")

add_library(lib_scene STATIC
  src/attachment.c
  src/attack.c
  src/blink.c
  src/bounds.c
  src/brain.c
  src/camera.c
  src/collision.c
  src/controller.c
  src/explosive.c
  src/faction.c
  src/footstep.c
  src/health.c
  src/knowledge.c
  src/level.c
  src/lifetime.c
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
  src/selection.c
  src/sensor.c
  src/skeleton.c
  src/sound.c
  src/spawner.c
  src/status.c
  src/tag.c
  src/target.c
  src/taunt.c
  src/terrain.c
  src/time.c
  src/transform.c
  src/unit.c
  src/vfx.c
  src/visibility.c
  src/weapon.c
  )
target_include_directories(lib_scene PUBLIC include)
target_link_libraries(lib_scene PUBLIC lib_asset lib_core lib_ecs lib_geo lib_ai)
target_link_libraries(lib_scene PRIVATE lib_log)

add_executable(test_lib_scene
  test/config.c
  test/test_brain.c
  test/test_nav.c
  )
target_link_libraries(test_lib_scene PRIVATE lib_app_check lib_scene)

configure_test(test_lib_scene SHORT_NAME scene)
configure_debuggable(test_lib_scene)
