#! /bin/dash

make -B SEL4CP_SDK=../../../sel4cp-sdk-1.2.6 \
        BOARD=qemu_arm_virt \
        TOOLCHAIN=aarch64-unknown-linux-gnu \
        -j8 \
        simulate

# qemu-system-aarch64 -machine virt,virtualization=on,highmem=off,secure=off \
#                         -cpu cortex-a53 \
#                         -serial mon:stdio \
#                         -device loader,file=build/loader.img,addr=0x70000000,cpu-num=0 \
#                         -m size=2G \
#                         -nographic

# qemu-system-aarch64 -machine virt,virtualization=on,highmem=off,secure=off \
# 			-cpu cortex-a53 \
# 			-serial mon:stdio \
# 			-device loader,file=build/loader.img,addr=0x70000000,cpu-num=0 \
# 			-m size=2G \
# 			-nographic \
# 			-hdb fat:../../tools

# qemu-system-aarch64 -machine virt,virtualization=on,highmem=off,secure=off \
# 			-cpu cortex-a53 \
# 			-serial mon:stdio \
# 			-device loader,file=build/loader.img,addr=0x70000000,cpu-num=0 \
# 			-m size=2G \
# 			-nographic \
# 			-drive file=fat:/Users/tarney/code/unsw/ts/sel4cp_vmm/examples/uio/vfat,format=raw,media=disk