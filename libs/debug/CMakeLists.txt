# --------------------------------------------------------------------------------------------------
# Debug library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: debug")

add_library(lib_debug STATIC
  src/animation.c
  src/asset.c
  src/brain.c
  src/camera.c
  src/ecs.c
  src/gizmo.c
  src/grid.c
  src/inspector.c
  src/interface.c
  src/light.c
  src/log_viewer.c
  src/menu.c
  src/prefab.c
  src/register.c
  src/rend.c
  src/shape.c
  src/stats.c
  src/text.c
  src/time.c
  src/widget.c
  )
target_include_directories(lib_debug PUBLIC include)
target_link_libraries(lib_debug PUBLIC lib_core lib_ecs lib_scene)
target_link_libraries(lib_debug PRIVATE lib_gap lib_log lib_rend lib_input lib_ui)
