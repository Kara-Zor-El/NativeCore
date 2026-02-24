{
  inputs = {
    flakelight.url = "github:nix-community/flakelight";
  };
  outputs = { flakelight, ... }:
    flakelight ./. ({ lib, ...}: {
      systems = lib.systems.flakeExposed;
      devShell = {
        packages = pkgs: with pkgs.llvmPackages_latest; [
          pkgs.cmake
          pkgs.pkg-config
          llvm
          clang
          pkgs.clang-tools
          pkgs.go-task
          pkgs.libusb1
          pkgs.sdl3
          pkgs.libffi
          pkgs.zstd
          pkgs.libxml2
          pkgs.slang
        ] ++ lib.optionals pkgs.stdenv.isLinux [
          pkgs.wayland
          pkgs.wayland-protocols
          pkgs.libGL
          pkgs.mesa
          pkgs.libxkbcommon
        ];
        shellHook = pkgs: let
          libs = with pkgs; [
            vulkan-loader
            wayland
            libxkbcommon
            libGL
          ];
        in ''
          export LLVM_DIR="${pkgs.llvmPackages_latest.llvm.dev}/lib/cmake/llvm"
          export CMAKE_PREFIX_PATH="${pkgs.llvmPackages_latest.llvm.dev}:${pkgs.llvmPackages_latest.clang}:$CMAKE_PREFIX_PATH"
          export CC="${pkgs.llvmPackages_latest.clang}/bin/clang"
          export CXX="${pkgs.llvmPackages_latest.clang}/bin/clang++"
        '' + lib.optionalString pkgs.stdenv.isLinux ''
          export PKG_CONFIG_PATH="${pkgs.wayland}/lib/pkgconfig:${pkgs.wayland-protocols}/share/pkgconfig:${pkgs.libxkbcommon}/lib/pkgconfig:${pkgs.libGL}/lib/pkgconfig:${pkgs.mesa}/lib/pkgconfig:$PKG_CONFIG_PATH"
          export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath libs}:$LD_LIBRARY_PATH"
        '';
      };
  });
}
