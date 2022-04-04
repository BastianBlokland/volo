# --------------------------------------------------------------------------------------------------
# CMake utility to find the xkbcommon (https://xkbcommon.org) x keyboard library.
# --------------------------------------------------------------------------------------------------

#
# Available for (most?) linux distributions that are using the x window-server.
# For debian based distributions: apt install libxkbcommon-dev
# More info: https://xkbcommon.org
#

findpkg(LIB "xkbcommon" HEADER "xkbcommon/xkbcommon.h")
