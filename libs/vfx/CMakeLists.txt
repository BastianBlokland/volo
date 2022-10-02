# --------------------------------------------------------------------------------------------------
# Vfx library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: vfx")

add_library(lib_vfx STATIC
  src/emitter.c
  src/register.c
  )
target_include_directories(lib_vfx PUBLIC include)
target_link_libraries(lib_vfx PUBLIC lib_scene lib_core lib_ecs)
target_link_libraries(lib_vfx PRIVATE lib_rend lib_log)
