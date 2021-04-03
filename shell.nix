with import <nixpkgs> {};

#stdenv.mkDerivation {
llvmPackages_11.stdenv.mkDerivation {
  name = "gbfs";
  nativeBuildInputs = [ meson ninja pkg-config llvmPackages_11.libcxxClang ];

  buildInputs = [
    fuse3 sqlite
  ];
}
