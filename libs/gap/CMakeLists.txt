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

#
# On linux use the xcb (https://xcb.freedesktop.org/) bindings for the x window server.
# For debian based systems:
# apt install libxcb1-dev libxcb-xkb-dev libxkbcommon-dev libxkbcommon-x11-dev libxcb-xfixes0-dev libxcb-icccm4-dev libxcb-randr0-dev libxcb-cursor-dev
#
if(${VOLO_PLATFORM} STREQUAL "linux")
  message(STATUS "> Configuring xcb (with xkb, xkbcommon, xkbcommon-x11, xfixes, icccm, randr and cursor-util extensions) package")
  find_package(xcb REQUIRED)
  find_package(xcb-cursor REQUIRED)
  find_package(xcb-icccm REQUIRED)
  find_package(xcb-randr REQUIRED)
  find_package(xcb-xfixes REQUIRED)
  find_package(xcb-xkb REQUIRED)
  find_package(xkbcommon REQUIRED)
  find_package(xkbcommon-x11 REQUIRED)
  target_link_libraries(lib_gap PRIVATE xcb-cursor)
  target_link_libraries(lib_gap PRIVATE xcb-icccm)
  target_link_libraries(lib_gap PRIVATE xcb-randr)
  target_link_libraries(lib_gap PRIVATE xcb-xfixes)
  target_link_libraries(lib_gap PRIVATE xcb-xkb)
  target_link_libraries(lib_gap PRIVATE xcb)
  target_link_libraries(lib_gap PRIVATE xkbcommon-x11)
  target_link_libraries(lib_gap PRIVATE xkbcommon)
endif()
