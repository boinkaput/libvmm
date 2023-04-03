#!/bin/zsh

qemu-system-aarch64 \
   -m 4G  \
   -machine xlnx-zcu102 \
   -nographic \
   -device loader,file=build/loader.img,addr=0x40000000,cpu-num=0 \
   -device loader,addr=0xfd1a0104,data=0x0000000e,data-len=4 \
   -serial mon:stdio

