{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
  };

  outputs =
    inputs@{ flake-parts, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "aarch64-darwin"
        "x86_64-darwin"
      ];
      perSystem =
        {
          pkgs,
          lib,
          system,
          ...
        }:
        let
          isLinux = lib.strings.hasSuffix "linux" system;
        in
        {
          devShells.default = pkgs.mkShell {
            nativeBuildInputs =
              with pkgs;
              [
                gnumake
                cmake
                ninja
                libcxx
                libllvm
                clang
              ]
              ++ lib.optionals isLinux [
                (lib.lowPrio xorg.xorgproto)
                libxcb.dev
                libxrandr
                libxinerama
                libx11
                libxcursor
                libxxf86vm
                libGL
                glfw

                libxi
              ];
          };
        };
    };
}
