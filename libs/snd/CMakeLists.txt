# --------------------------------------------------------------------------------------------------
# Sound library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: snd")

add_library(lib_snd STATIC
  src/device.c
  )
target_include_directories(lib_snd PUBLIC include)
target_link_libraries(lib_snd PUBLIC lib_core)
target_link_libraries(lib_snd PRIVATE lib_log)
