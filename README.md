[![test](https://github.com/BastianBlokland/volo/actions/workflows/test.yaml/badge.svg)](https://github.com/BastianBlokland/volo/actions/workflows/test.yaml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

*Work in progress toy 3d engine*

# Volo

## Building

### Supported platforms
* Linux + X11 (*Ports to other POSIX + x11 platforms should be doable, but linux specific apis are used at the moment*).
* Microsoft Windows (*8.1 or newer*).

### Dependencies
* `C` compiler supporting `c11`: Tested on recent [`gcc`](https://gcc.gnu.org/), [`clang`](https://clang.llvm.org/) and [`msvc`](https://docs.microsoft.com/en-us/cpp/build/reference/c-cpp-building-reference?).
* Meta build-system: [CMake](https://cmake.org/) (*3.19 or newer*).
* Build-system: Tested with [`ninja`](https://ninja-build.org/manual.html), [`make`](https://www.gnu.org/software/make/), [`nmake`](https://docs.microsoft.com/en-us/cpp/build/reference/nmake-reference) and [`msbuild`](https://docs.microsoft.com/en-us/visualstudio/msbuild) (`Ninja` comes highly recommended).
* [Vulkan SDK](https://vulkan.lunarg.com/).
* (*Linux only*) [XCB](https://xcb.freedesktop.org/) (X protocol bindings) + various extensions.

### Linux
* Install a `c` compiler and build-system (debian: `apt install build-essential`).
* Install `CMake` (debian: `apt install cmake`).
* Install the `Vulkan` sdk (source: https://vulkan.lunarg.com/sdk/home#linux).
* Install `XCB` + `xkb`, `xkbcommon`, `xkbcommon-x11`, `xfixes`, `icccm`, `randr` and `cursor-util` extensions
  (debian: `apt install libxcb1-dev libxcb-xkb-dev libxkbcommon-dev libxkbcommon-x11-dev libxcb-xfixes0-dev libxcb-icccm4-dev libxcb-randr0-dev libxcb-cursor-dev`).
* Build and run: `scripts/run-linux.sh` (or invoke `cmake` and your buildsystem manually).

### Windows
* (*Optional*) Install the `winget` package manager (info: https://docs.microsoft.com/en-us/windows/package-manager/winget/).
* Install `Visual Studio Build Tools` (winget: `winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows10SDK.19041 --focusedUi"`)
* Install `CMake` (winget: `winget install Kitware.CMake`).
* Install the `Vulkan` sdk (winget: `winget install KhronosGroup.VulkanSDK`).
* Build and run: `scripts/run-win32.bat` (or invoke `cmake` and your buildsystem manually)..

### Tests

Unit tests can be invoked using the `test.[libname]` targets (for example `test.core` or `test.geo`).
To run all the tests there's an overarching `test` target.
