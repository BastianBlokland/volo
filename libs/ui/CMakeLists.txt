# --------------------------------------------------------------------------------------------------
# Ui library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(ui STATIC
  src/builder.c
  src/canvas.c
  src/cmd.c
  src/color.c
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
target_include_directories(ui PUBLIC include)
target_link_libraries(ui PUBLIC asset core ecs)
target_link_libraries(ui PRIVATE gap log rend input snd loc)
