#! /bin/dash
# ~/code/unsw/ts/sel4cp-sdk-1.2.6 \ doesn't work?
make -B SEL4CP_SDK=../../../sel4cp-sdk-1.2.6 \
        BOARD=qemu_arm_virt \
        -j8 \
        qemu
