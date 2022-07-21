[![test](https://github.com/BastianBlokland/volo/actions/workflows/test.yaml/badge.svg)](https://github.com/BastianBlokland/volo/actions/workflows/test.yaml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

*Work in progress toy 3d engine*

# Volo

## Building

### Supported platforms

* Linux + X11 (*Ports to other POSIX + x11 platforms should be doable, but linux specific apis are used at the moment*).
* Microsoft Windows (*8.1 or newer*).

### Dependencies

* C compiler supporting c11: Tested on recent [**gcc**](https://gcc.gnu.org/), [**clang**](https://clang.llvm.org/) and [**msvc**](https://docs.microsoft.com/en-us/cpp/build/reference/c-cpp-building-reference?).
* Meta build-system: [CMake](https://cmake.org/) (3.19 or newer).
* Build-system: Tested with [**ninja**](https://ninja-build.org/manual.html), [**make**](https://www.gnu.org/software/make/), [**nmake**](https://docs.microsoft.com/en-us/cpp/build/reference/nmake-reference) and [**msbuild**](https://docs.microsoft.com/en-us/visualstudio/msbuild).
* [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/).
* [XCB](https://xcb.freedesktop.org/) (X protocol bindings) + various extensions (Linux only).
