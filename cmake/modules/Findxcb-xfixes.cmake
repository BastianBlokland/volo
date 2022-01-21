# --------------------------------------------------------------------------------------------------
# CMake utility to find the xfixes extension for xcb (https://xcb.freedesktop.org/).
# --------------------------------------------------------------------------------------------------

#
# Available for (most?) linux distributions that are using the x window-server.
# For debian based distributions: apt install libxcb-xfixes0-dev
# More info: https://xcb.freedesktop.org/
#

findpkg(LIB "xcb-xfixes" HEADER "xcb/xfixes.h")
