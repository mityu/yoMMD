{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  };

  outputs =
    { nixpkgs, ... }:
    let
      forEachDarwinSystem = nixpkgs.lib.genAttrs [
        "aarch64-darwin"
        "x86_64-darwin"
        "x86_64-linux"
      ];
    in
    {
      devShells = forEachDarwinSystem (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        {
          default = pkgs.mkShell {
            nativeBuildInputs = with pkgs; [
              libcxx
              libllvm
              clang
              gnumake
              cmake
              ninja
              libglvnd
            ];
          };
          cross-to-windows = pkgs.mkShell {
            nativeBuildInputs = with pkgs.pkgsCross.mingwW64; [
              stdenv.cc
              cmake
              ninja
              gnumake
              libglvnd
              glfw
              # windows.pthreads
            ];
            # buildInputs = [
            #   (pkgs.pkgsCross.mingwW64.windows.mcfgthreads.overrideAttrs {
            #     dontDisableStatic = true;
            #   })
            # ];
            # buildInputs = [
            #   pkgs.pkgsCross.mingwW64.threads
            # ];
          };
        }
      );
    };
}
