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
# apt install libxcb1-dev libxcb-xkb-dev libxkbcommon-dev libxkbcommon-x11-dev libxcb-randr0-dev
#
if(${VOLO_PLATFORM} STREQUAL "linux")

  message(STATUS "> Configuring xcb (with xkb, xkbcommon, xkbcommon-x11 and randr extensions) package")
  find_package(xcb REQUIRED)
  find_package(xcb-randr REQUIRED)
  find_package(xcb-xkb REQUIRED)
  find_package(xkbcommon REQUIRED)
  find_package(xkbcommon-x11 REQUIRED)
  target_link_libraries(lib_gap PRIVATE
    xcb
    xcb-randr
    xcb-xkb
    xkbcommon
    xkbcommon-x11
    )

endif()
