# --------------------------------------------------------------------------------------------------
# CMake utility to find the RandR extension for xcb (https://xcb.freedesktop.org/) library.
# --------------------------------------------------------------------------------------------------

#
# Available for (most?) linux distributions that are using the x window-server.
# For debian based distributions: apt install libxcb-randr0-dev
# More info: https://xcb.freedesktop.org/manual/group__XCB__RandR__API.html
#

findpkg(LIB "xcb-randr" HEADER "xcb/randr.h")
