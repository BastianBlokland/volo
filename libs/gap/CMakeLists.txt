# --------------------------------------------------------------------------------------------------
# Gui Application Protocol library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(gap STATIC
  src/error.c
  src/input.c
  src/pal.c
  src/platform.c
  src/register.c
  src/window.c
  )
target_include_directories(gap PUBLIC include)
target_link_libraries(gap PUBLIC core ecs)
target_link_libraries(gap PRIVATE log asset)
