# --------------------------------------------------------------------------------------------------
# Gui Application Protocol library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: gap")

add_library(lib_gap STATIC
  src/input.c
  src/pal.c
  src/platform.c
  src/register.c
  src/window.c
  )
target_include_directories(lib_gap PUBLIC include)
target_link_libraries(lib_gap PUBLIC lib_core lib_ecs)
target_link_libraries(lib_gap PRIVATE lib_log lib_asset)

#
# On linux use the xcb (https://xcb.freedesktop.org/) bindings for the x window server.
# For debian based systems:
# apt install libxcb1-dev
#
if(${VOLO_PLATFORM} STREQUAL "linux")

  message(STATUS "> Configuring xcb package")
  find_package(xcb REQUIRED)
  target_link_libraries(lib_gap PRIVATE xcb)

endif()
