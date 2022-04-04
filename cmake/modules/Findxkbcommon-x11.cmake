# --------------------------------------------------------------------------------------------------
# CMake utility to find the x11 extension for the xkbcommon (https://xkbcommon.org) library.
# --------------------------------------------------------------------------------------------------

#
# Available for (most?) linux distributions that are using the x window-server.
# For debian based distributions: apt install libxkbcommon-x11-dev
# More info: https://xkbcommon.org
#

findpkg(LIB "xkbcommon-x11" HEADER "xkbcommon/xkbcommon-x11.h")
