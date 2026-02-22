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
        ];
        shellHook = pkgs: ''
          export LLVM_DIR="${pkgs.llvmPackages_latest.llvm.dev}/lib/cmake/llvm"
          export CMAKE_PREFIX_PATH="${pkgs.llvmPackages_latest.llvm.dev}:${pkgs.llvmPackages_latest.clang}:$CMAKE_PREFIX_PATH"
        '';
      };
  });
}
