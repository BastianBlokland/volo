# --------------------------------------------------------------------------------------------------
# CMake utility to find the cursor-util extension for xcb (https://xcb.freedesktop.org/) library.
# --------------------------------------------------------------------------------------------------

#
# Available for (most?) linux distributions that are using the x window-server.
# For debian based distributions: apt install libxcb-cursor-dev
# More info: https://gitlab.freedesktop.org/xorg/lib/libxcb-cursor/
#

findpkg(LIB "xcb-cursor" HEADER "xcb/xcb_cursor.h")
