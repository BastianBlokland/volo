# --------------------------------------------------------------------------------------------------
# Sound library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: snd")

add_library(lib_snd STATIC
  src/buffer.c
  src/channel.c
  src/device_pal.c
  src/device.c
  src/mixer.c
  src/register.c
  )
target_include_directories(lib_snd PUBLIC include)
target_link_libraries(lib_snd PUBLIC lib_core lib_ecs)
target_link_libraries(lib_snd PRIVATE lib_log lib_scene)

#
# On linux use the 'asound' libray for the Alsa audio architecture (https://alsa-project.org/).
# For debian based systems: apt install libasound2-dev libasound2
#
if(${VOLO_PLATFORM} STREQUAL "linux")

  message(STATUS "> Configuring Alsa")
  find_package(asound REQUIRED)
  target_link_libraries(lib_gap PRIVATE asound)

endif()
