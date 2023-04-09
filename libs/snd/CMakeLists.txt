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
  src/source.c
  )
target_include_directories(lib_snd PUBLIC include)
target_link_libraries(lib_snd PUBLIC lib_core lib_ecs)
target_link_libraries(lib_snd PRIVATE lib_log lib_scene)

if(${VOLO_PLATFORM} STREQUAL "linux")
  #
  # On linux use the 'asound' libray for the Alsa audio architecture (https://alsa-project.org/).
  # For debian based systems: apt install libasound2-dev libasound2
  #
  message(STATUS "> Configuring Alsa")
  find_package(asound REQUIRED)
  target_link_libraries(lib_snd PRIVATE asound)

elseif(${VOLO_PLATFORM} STREQUAL "win32")

  # Link against 'winmm.lib' (Windows Multimedia API).
  target_link_libraries(lib_snd PRIVATE winmm.lib)

endif()
