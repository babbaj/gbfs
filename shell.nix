{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
    
    nativeBuildInputs = with pkgs; [ 
      fuse3 sqlite 
      cmake pkg-config 
      #gcc10
      clang_11
    ];

    shellHook = ''
        CC=${pkgs.clang_11}/bin/cc
        CXX=${pkgs.clang_11}/bin/c++
    '';
}