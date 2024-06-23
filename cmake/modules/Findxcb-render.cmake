# --------------------------------------------------------------------------------------------------
# CMake utility to find the render extension for the xcb (https://xcb.freedesktop.org/) library.
# --------------------------------------------------------------------------------------------------

#
# Available for (most?) linux distributions that are using the x window-server.
# For debian based distributions: apt install libxcb-render0-dev
# More info: https://xcb.freedesktop.org/manual/group__XCB__Render__API.html
#

findpkg(LIB "xcb-render" HEADER "xcb/render.h")
