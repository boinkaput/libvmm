#! /bin/dash

make -B SEL4CP_SDK=../../../sel4cp-sdk-1.2.6 BOARD=odroidc4 TOOLCHAIN=aarch64-unknown-linux-gnu -j8

# mq.sh run -c "buildroot" -a -l mqlog -s odroidc4_1 -f build/loader.img
# mq.sh run -c "buildroot" -a -l mqlog -s odroidc4_pool -f build/loader.img