{
  description = "Volo Nix Dev Environment";

  inputs = {
    nixpkgs.url = "nixpkgs/nixos-25.11";
    rust-overlay = {
      url = "github:oxalica/rust-overlay";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    { nixpkgs, rust-overlay, ... }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        overlays = [ rust-overlay.overlays.default ];
      };
      llvmPkg = pkgs.llvmPackages_21;
    in
    {
      devShells.${system} = rec {

        llvm = (pkgs.mkShellNoCC.override { stdenv = llvmPkg.stdenv; }) {

          packages = [
            pkgs.nixfmt-rfc-style
            pkgs.clang-tools

            llvmPkg.lld
            llvmPkg.lldb
            llvmPkg.clang

            pkgs.cmake
            pkgs.ninja

            pkgs.alsa-lib
            pkgs.elfutils
            pkgs.libxkbcommon
            pkgs.openssl
            pkgs.shaderc
            pkgs.vulkan-loader
            pkgs.vulkan-tools
            pkgs.wayland-protocols
            pkgs.wayland-scanner
            pkgs.xorg.libxcb
            pkgs.xorg.xcbutilkeysyms

            # Ide extension dependencies:
            pkgs.nodejs_24
            (pkgs.rust-bin.stable.latest.default.override {
              targets = [ "wasm32-wasip2" ];
            })
          ];

          # Configure runtime dependencies.
          NIX_LDFLAGS = "-rpath ${
            pkgs.lib.makeLibraryPath [
              pkgs.elfutils
              pkgs.openssl
              pkgs.shaderc
              pkgs.vulkan-loader
              pkgs.xorg.libxcb
              pkgs.xorg.xcbutilkeysyms
              pkgs.libxkbcommon
              pkgs.alsa-lib
              pkgs.wayland
            ]
          }";

          VK_LOADER_DEBUG = "error"; # error,warn,info

          # Variables for the run.wlgen CMake target.
          VOLO_WAYLAND_DATADIR = "${pkgs.wayland-scanner}/share/wayland";
          VOLO_WAYLAND_PROTOCOLS_DATADIR = "${pkgs.wayland-protocols}/share/wayland-protocols";
        };

        default = llvm;
      };
    };
}
