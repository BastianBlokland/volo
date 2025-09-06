# --------------------------------------------------------------------------------------------------
# Scene library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(scene STATIC
  src/action.c
  src/attachment.c
  src/attack.c
  src/bark.c
  src/bounds.c
  src/camera.c
  src/collision.c
  src/creator.c
  src/debug.c
  src/faction.c
  src/footstep.c
  src/health.c
  src/level.c
  src/lifetime.c
  src/light.c
  src/location.c
  src/locomotion.c
  src/marker.c
  src/name.c
  src/nav.c
  src/prefab.c
  src/product.c
  src/projectile.c
  src/property.c
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
target_include_directories(scene PUBLIC include)
target_link_libraries(scene PUBLIC asset core ecs geo)
target_link_libraries(scene PRIVATE log trace)

add_executable(scene_test
  test/config.c
  test/test_nav.c
  test/test_script.c
  test/test_set.c
  )
target_link_libraries(scene_test PRIVATE app_check scene)
