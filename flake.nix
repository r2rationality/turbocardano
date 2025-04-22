{
  inputs = {
    nixpkgs = {
      url = "github:nixos/nixpkgs/nixos-unstable";
    };
    flake-utils = {
      url = "github:numtide/flake-utils";
    };
  };
  outputs = { nixpkgs, flake-utils, ... }: flake-utils.lib.eachDefaultSystem (system:
    let
      pkgs = import nixpkgs {
        inherit system;
      };
      daedalus-turbo = (with pkgs; stdenv.mkDerivation {
          pname = "daedalus-turbo";
          version = "0.0.0";
          src = ./.;
          nativeBuildInputs = [
            clang
            cmake
            pkg-config
          ];
          buildInputs = [
            boost183
            libsodium
            spdlog
            fmt
            zstd
          ];
          installPhase = ''
            mkdir -p $out/bin
            cp dt $out/bin
            ln -s $out/bin/dt $out/bin/daedalus-turbo
          '';
        }
      );
    in rec {
      defaultApp = flake-utils.lib.mkApp {
        drv = defaultPackage;
      };
      defaultPackage = daedalus-turbo;
      devShell = pkgs.mkShell {
        buildInputs = [
           daedalus-turbo
        ];
      };
    }
  );
}
