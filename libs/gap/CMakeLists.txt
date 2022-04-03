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
target_link_libraries(lib_gap PRIVATE lib_log)
target_link_libraries(lib_gap PUBLIC lib_core)
target_link_libraries(lib_gap PUBLIC lib_ecs)

# On linux use the xcb (https://xcb.freedesktop.org/) bindings for the x window server.
# For debian based systems: apt install libxcb1-dev libxcb-xkb-dev libxkbcommon-dev libxcb-xfixes0-dev libxcb-icccm4-dev
if(${VOLO_PLATFORM} STREQUAL "linux")
  message(STATUS "> Configuring xcb (with xkb, xkbcommon, xfixes and icccm extensions) package")
  find_package(xcb REQUIRED)
  find_package(xcb-xkb REQUIRED)
  find_package(xkbcommon REQUIRED)
  find_package(xcb-xfixes REQUIRED)
  find_package(xcb-icccm REQUIRED)
  target_link_libraries(lib_gap PRIVATE xcb-icccm)
  target_link_libraries(lib_gap PRIVATE xcb-xfixes)
  target_link_libraries(lib_gap PRIVATE xcb-xkb)
  target_link_libraries(lib_gap PRIVATE xkbcommon)
  target_link_libraries(lib_gap PRIVATE xcb)
endif()
