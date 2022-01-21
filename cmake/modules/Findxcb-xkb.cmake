# --------------------------------------------------------------------------------------------------
# CMake utility to find the xkb extension for xcb (https://xcb.freedesktop.org/) library.
# --------------------------------------------------------------------------------------------------

#
# Available for (most?) linux distributions that are using the x window-server.
# For debian based distributions: apt install libxcb-xkb-dev
# More info: https://xcb.freedesktop.org/
#

findpkg(LIB "xcb-xkb" HEADER "xcb/xkb.h")
