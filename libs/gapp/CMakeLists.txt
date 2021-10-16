# --------------------------------------------------------------------------------------------------
# GuiApp library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: gapp")

add_library(volo_gapp STATIC
  src/pal.c
  src/platform.c
  src/register.c
  src/window.c)
target_include_directories(volo_gapp PUBLIC include)
target_link_libraries(volo_gapp PUBLIC volo_core)
target_link_libraries(volo_gapp PUBLIC volo_ecs)
target_link_libraries(volo_gapp PUBLIC volo_log)

# On linux use the xcb (https://xcb.freedesktop.org/) bindings for the x window server.
# For debian based systems: apt install libxcb1-dev libxcb-xkb-dev
if(${VOLO_PLATFORM} STREQUAL "linux")
  message(STATUS "> Configuring xcb (with xkb) package")
  find_package(xcb REQUIRED)
  find_package(xcb-xkb REQUIRED)
  target_link_libraries(volo_gapp PRIVATE xcb)
  target_link_libraries(volo_gapp PRIVATE xcb-xkb)
endif()

message(STATUS "> demos: gapp")
add_subdirectory(demos)
