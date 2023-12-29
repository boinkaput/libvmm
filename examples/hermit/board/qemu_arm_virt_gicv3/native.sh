qemu-system-aarch64 \
                  -machine virt,gic-version=3 \
                  -cpu cortex-a53 \
                  -m 512M  \
                  -semihosting \
                  -display none -serial stdio \
                  -kernel /home/ivanv/ts/hermit/hermit-loader-aarch64 \
                  -device guest-loader,addr=0x48000000,initrd=hermit-hello-world

