# --------------------------------------------------------------------------------------------------
# CMake utility to find the xcb-xkb (https://xcb.freedesktop.org/) library.
# --------------------------------------------------------------------------------------------------

#
# Available for (most?) linux distributions that are using the x window-server.
# For debian based distributions: apt install libxcb-xkb-dev
# More info: https://xcb.freedesktop.org/
#

findpkg("xcb-xkb" "xcb/xkb.h")
