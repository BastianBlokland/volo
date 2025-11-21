{
  description = "Volo Nix Dev Environment";

  inputs = {
    nixpkgs.url = "nixpkgs/nixos-25.05";
  };

  outputs =
    { nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
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

            pkgs.elfutils
            pkgs.shaderc
            pkgs.vulkan-tools
            pkgs.openssl
            pkgs.vulkan-loader
            pkgs.xorg.libxcb
            pkgs.xorg.xcbutilkeysyms
            pkgs.libxkbcommon
            pkgs.alsa-lib
          ];

          LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath [
            pkgs.elfutils
            pkgs.openssl
            pkgs.shaderc
            pkgs.vulkan-loader
            pkgs.xorg.libxcb
            pkgs.xorg.xcbutilkeysyms
            pkgs.libxkbcommon
            pkgs.alsa-lib
          ];

          VK_LOADER_DEBUG = "error"; # error,warn,info
        };

        default = llvm;
      };
    };
}
