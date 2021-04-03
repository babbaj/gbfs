{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
    
    nativeBuildInputs = with pkgs; [ 
      fuse3 sqlite 
      cmake pkg-config 
      gcc10
      #clang_11
    ];
}