make -B \
	BUILD_DIR=build \
	SEL4CP_SDK=../sel4cp_dev/release/sel4cp-sdk-1.2.6 \
	SEL4CP_CONFIG=debug \
	SEL4CP_BOARD=qemu_riscv_virt_hyp \
	SYSTEM=simple.system \
    	ARCH=riscv64 \
	-j

