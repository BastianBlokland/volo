# --------------------------------------------------------------------------------------------------
# Sound library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(snd STATIC
  src/buffer.c
  src/channel.c
  src/device_pal.c
  src/device.c
  src/mixer.c
  src/register.c
  src/result.c
  src/source.c
  )
target_include_directories(snd PUBLIC include)
target_link_libraries(snd PUBLIC core ecs)
target_link_libraries(snd PRIVATE log scene)
