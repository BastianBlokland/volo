# --------------------------------------------------------------------------------------------------
# CMake utility to find the xcb-xkb (https://xcb.freedesktop.org/) library.
# --------------------------------------------------------------------------------------------------

#
# Available for (most?) linux distributions that are using the x window-server.
# For example for debian based distributions: apt install libxcb1-dev
# More info: https://xcb.freedesktop.org/
#

findpkg(LIB "xcb" HEADER "xcb/xcb.h")
