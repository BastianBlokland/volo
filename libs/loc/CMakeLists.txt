# --------------------------------------------------------------------------------------------------
# Locale library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(loc STATIC
  src/manager.c
  src/register.c
  src/translate.c
  )
target_include_directories(loc PUBLIC include)
target_link_libraries(loc PUBLIC core ecs)
target_link_libraries(loc PRIVATE asset log)
