# --------------------------------------------------------------------------------------------------
# Ui library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: ui")

add_library(lib_ui STATIC
  src/builder.c
  src/canvas.c
  src/cmd.c
  src/escape.c
  src/layout.c
  src/register.c
  src/resource.c
  src/shape.c
  src/style.c
  src/text.c
  src/widget.c
  )
target_include_directories(lib_ui PUBLIC include)
target_link_libraries(lib_ui PRIVATE lib_gap)
target_link_libraries(lib_ui PRIVATE lib_log)
target_link_libraries(lib_ui PRIVATE lib_rend)
target_link_libraries(lib_ui PUBLIC lib_asset)
target_link_libraries(lib_ui PUBLIC lib_core)
target_link_libraries(lib_ui PUBLIC lib_ecs)
