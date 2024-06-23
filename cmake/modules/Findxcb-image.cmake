# --------------------------------------------------------------------------------------------------
# CMake utility to find the Image extension for xcb (https://xcb.freedesktop.org/) library.
# --------------------------------------------------------------------------------------------------

#
# Available for (most?) linux distributions that are using the x window-server.
# For debian based distributions: apt install libxcb-image0-dev
# More info: https://gitlab.freedesktop.org/xorg/lib/libxcb-image
#

findpkg(LIB "xcb-image" HEADER "xcb/xcb_image.h")
