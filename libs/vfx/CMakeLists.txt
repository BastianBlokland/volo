# --------------------------------------------------------------------------------------------------
# Vfx library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: vfx")

add_library(lib_vfx STATIC
  src/particle.c
  src/register.c
  src/system.c
  )
target_include_directories(lib_vfx PUBLIC include)
target_link_libraries(lib_vfx PUBLIC lib_asset lib_scene lib_core lib_ecs)
target_link_libraries(lib_vfx PRIVATE lib_rend lib_log)
