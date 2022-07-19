# --------------------------------------------------------------------------------------------------
# Ui library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: ui")

add_library(lib_ui STATIC
  src/builder.c
  src/canvas.c
  src/cmd.c
  src/editor.c
  src/escape.c
  src/layout.c
  src/panel.c
  src/register.c
  src/resource.c
  src/scrollview.c
  src/settings.c
  src/shape.c
  src/stats.c
  src/style.c
  src/table.c
  src/text.c
  src/widget.c
  )
target_include_directories(lib_ui PUBLIC include)
target_link_libraries(lib_ui PUBLIC lib_asset lib_core lib_ecs)
target_link_libraries(lib_ui PRIVATE lib_gap lib_log lib_rend lib_input)
