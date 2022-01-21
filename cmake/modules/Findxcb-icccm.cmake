# --------------------------------------------------------------------------------------------------
# CMake utility to find the icccm extension for xcb (https://xcb.freedesktop.org/) library.
# --------------------------------------------------------------------------------------------------

#
# Available for (most?) linux distributions that are using the x window-server.
# For debian based distributions: apt install libxcb-icccm4-dev
# More info: https://tronche.com/gui/x/icccm/
# More info: https://xcb.freedesktop.org/
#

findpkg(LIB "xcb-icccm" HEADER "xcb/xcb_icccm.h")
