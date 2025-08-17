# --------------------------------------------------------------------------------------------------
# Input library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(input STATIC
  src/manager.c
  src/register.c
  src/resource.c
  )
target_include_directories(input PUBLIC include)
target_link_libraries(input PUBLIC core ecs)
target_link_libraries(input PRIVATE asset log gap)
