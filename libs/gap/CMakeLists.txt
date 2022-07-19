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
target_link_libraries(lib_gap PRIVATE lib_log)

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
  target_link_libraries(lib_gap PRIVATE
    xcb-cursor
    xcb-icccm
    xcb-randr
    xcb-xfixes
    xcb-xkb
    xcb
    xkbcommon-x11
    xkbcommon
    )

elseif(${VOLO_PLATFORM} STREQUAL "win32")

  # Link against 'shcore.lib' for dpi awareness apis.
  # NOTE: Exclude the gcc compiler because 'shcore.lib' is not available under MinGW at the moment.
  if(NOT ${VOLO_COMPILER} STREQUAL "gcc")
    target_link_libraries(lib_gap PRIVATE shcore.lib)
  endif()

endif()
