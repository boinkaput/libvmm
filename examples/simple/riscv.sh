qemu-system-riscv64 \
       -machine virt \
       -m size=3072M \
       -nographic \
       -serial mon:stdio \
       -kernel build/loader.img
