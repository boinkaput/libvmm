let
  pkgs = import <nixpkgs> {};
in
  pkgs.mkShell {
    nativeBuildInputs =
    let
      crossInputs = with pkgs.pkgsCross.aarch64-multiplatform; [
        alsa-lib
      ];
      nativeInputs = with pkgs; [
        llvm
        dtc
        qemu
        patchelf
        clang
        lld
        gzip
        fakeroot
        cpio
        zig
        perl
      ];
    in crossInputs ++ nativeInputs;
}
