# --------------------------------------------------------------------------------------------------
# Development library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(dev STATIC
  src/asset.c
  src/camera.c
  src/ecs.c
  src/finder.c
  src/gizmo.c
  src/grid.c
  src/hierarchy.c
  src/input.c
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
target_include_directories(dev PUBLIC include)
target_link_libraries(dev PUBLIC core ecs scene log)
target_link_libraries(dev PRIVATE gap rend input ui snd trace vfx)
