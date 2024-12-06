# --------------------------------------------------------------------------------------------------
# Debug library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: debug")

add_library(lib_debug STATIC
  src/asset.c
  src/camera.c
  src/ecs.c
  src/gizmo.c
  src/grid.c
  src/inspector.c
  src/interface.c
  src/level.c
  src/log_viewer.c
  src/menu.c
  src/panel.c
  src/prefab.c
  src/register.c
  src/rend.c
  src/script.c
  src/shape.c
  src/skeleton.c
  src/sound.c
  src/stats.c
  src/text.c
  src/time.c
  src/trace.c
  src/vfx.c
  src/widget.c
  )
target_include_directories(lib_debug PUBLIC include)
target_link_libraries(lib_debug PUBLIC lib_core lib_ecs lib_scene lib_log)
target_link_libraries(lib_debug PRIVATE lib_gap lib_rend lib_input lib_ui lib_snd lib_trace lib_vfx)
