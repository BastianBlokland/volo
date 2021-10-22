# --------------------------------------------------------------------------------------------------
# Gui Application Protocol library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: gap")

add_library(volo_gap STATIC
  src/input.c
  src/pal.c
  src/register.c
  src/window.c)
target_include_directories(volo_gap PUBLIC include)
target_link_libraries(volo_gap PUBLIC volo_core)
target_link_libraries(volo_gap PUBLIC volo_ecs)
target_link_libraries(volo_gap PUBLIC volo_log)

# On linux use the xcb (https://xcb.freedesktop.org/) bindings for the x window server.
# For debian based systems: apt install libxcb1-dev libxcb-xkb-dev libxcb-xfixes0-dev
if(${VOLO_PLATFORM} STREQUAL "linux")
  message(STATUS "> Configuring xcb (with xkb) package")
  find_package(xcb REQUIRED)
  find_package(xcb-xkb REQUIRED)
  target_link_libraries(volo_gap PRIVATE xcb)
  target_link_libraries(volo_gap PRIVATE xcb-xkb)
  target_link_libraries(volo_gap PRIVATE xcb-xfixes)
endif()

message(STATUS "> demos: gap")
add_subdirectory(demos)
