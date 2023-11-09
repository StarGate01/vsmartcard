{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs";
  };

  outputs = { self, nixpkgs }:
    let
      pkgs = import nixpkgs {
        system = "x86_64-linux";
        config.permittedInsecurePackages = [
            "openssl-1.1.1w"
        ];
      };
    in
    {
      devShell.x86_64-linux =
        pkgs.mkShell {
          shellHook = ''
            export CFLAGS=-Wno-error=use-after-free
          '';

          buildInputs = with pkgs; [
            stdenv.cc
            pcsclite
            gengetopt
            openssl_1_1
            libtool
            pkg-config
            help2man
            autoconf
            automake
            gnumake
          ];
        };
    };
}
