QEMU := qemu-system-aarch64
QEMU_SND_BACKEND := coreaudio
QEMU_SND_FRONTEND := hda

MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
SYSTEM_DIR := $(EXAMPLE_DIR)/board/$(MICROKIT_BOARD)
SYSTEM_FILE := $(SYSTEM_DIR)/virtio-snd.system
IMAGE_FILE := loader.img
REPORT_FILE := report.txt

SOUND_COMPONENTS := $(SDDF)/sound/components
SND_DRIVER_VM_DIR := $(SYSTEM_DIR)/snd_driver_vm
SND_UIO_DRIVERS_DIR := $(LINUX_TOOLS_DIR)/uio_drivers/snd
SND_DRIVER_VM_ASOUND_CONFIG := $(LINUX_TOOLS_DIR)/snd/board/$(MICROKIT_BOARD)/asound.conf

ifeq ($(strip $(MICROKIT_BOARD)), odroidc4)
	UART_DRIVER_DIR := meson
else ifeq ($(strip $(MICROKIT_BOARD)), qemu_arm_virt)
	UART_DRIVER_DIR := arm
else
$(error Unsupported MICROKIT_BOARD given)
endif

SERIAL_COMPONENTS := $(SDDF)/serial/components
UART_DRIVER := $(SDDF)/drivers/serial/$(UART_DRIVER_DIR)
SERIAL_CONFIG_INCLUDE := $(EXAMPLE_DIR)/include/serial_config

vpath %.c $(LIBVMM) $(EXAMPLE_DIR)

IMAGES :=	benchmark.elf \
			idle.elf \
			native_client.elf \
			sound_virt.elf \
			snd_driver_vmm.elf \
			serial_virt_tx.elf \
			serial_virt_rx.elf \
			uart_driver.elf

SND_DRIVER_VM_USERLEVEL_IMAGES := user_sound.elf control.elf pcm_min.elf pcm.elf record.elf feedback.elf

CFLAGS := \
	  -mstrict-align \
	  -ffreestanding \
	  -fno-stack-protector \
	  -g3 -O3 -Wall \
	  -Wno-unused-function \
	  -DMICROKIT_CONFIG_$(MICROKIT_CONFIG) \
	  -DBOARD_$(MICROKIT_BOARD) \
	  -I$(BOARD_DIR)/include \
	  -I$(LIBVMM)/include \
	  -I$(LINUX_TOOLS_DIR)/include \
	  -I$(SDDF)/include \
	  -I$(SERIAL_CONFIG_INCLUDE) \
	  -MD \
	  -MP \
	  -target $(TARGET)

CFLAGS_USERLEVEL := \
			-mstrict-align \
	  		-ffreestanding \
			-g3 -O3 -Wall \
			-Wall -Wno-unused-function \
			-I$(SDDF)/include \
			-I$(LIBVMM)/include \
			-I$(LINUX_TOOLS_DIR)/include \
			-lasound \
			-lm \
			-target $(TARGET_USERLEVEL) \
			$(NIX_LDFLAGS) \
			$(NIX_CFLAGS_COMPILE)

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := --start-group -lmicrokit -Tmicrokit.ld libvmm.a libsddf_util_debug.a --end-group

CHECK_FLAGS_BOARD_MD5 := .board_cflags-$(shell echo -- $(CFLAGS) $(CFLAGS_USERLEVEL) $(BOARD) $(MICROKIT_CONFIG) | shasum | sed 's/ *-//')

$(CHECK_FLAGS_BOARD_MD5):
	-rm -f .board_cflags-*
	touch $@


all: $(IMAGE_FILE)

$(IMAGES): libvmm.a libsddf_util_debug.a $(CHECK_FLAGS_BOARD_MD5)

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

benchmark.o: $(EXAMPLE_DIR)/benchmark.c $(CHECK_FLAGS_BOARD_MD5)
	$(CC) $(CFLAGS) -c -o $@ $<

benchmark.elf: benchmark.o libsddf_util.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

idle.o: $(EXAMPLE_DIR)/idle.c $(CHECK_FLAGS_BOARD_MD5)
	$(CC) $(CFLAGS) -c -o $@ $<

idle.elf: idle.o libsddf_util_debug.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

native_client.o: $(EXAMPLE_DIR)/native_client.c $(CHECK_FLAGS_BOARD_MD5)
	$(CC) $(CFLAGS) -c -o $@ $<

native_client.elf: native_client.o libsddf_util.a
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

snd_driver_vm.dts: $(SND_DRIVER_VM_DIR)/dts/linux.dts $(SND_DRIVER_VM_DIR)/dts/init.dts $(SND_DRIVER_VM_DIR)/dts/io.dts
	$(LIBVMM)/tools/dtscat $^ > $@

snd_driver_vm.dtb: snd_driver_vm.dts
	$(DTC) -q -I dts -O dtb $< > $@

snd_driver_vm_rootfs.cpio.gz: $(LINUX_TOOLS_DIR)/snd/sound $(SND_DRIVER_VM_USERLEVEL_IMAGES) $(SND_DRIVER_VM_ASOUND_CONFIG)
	$(LIBVMM)/tools/packrootfs $(SND_DRIVER_VM_DIR)/rootfs.cpio.gz snd_driver_vm_rootfs -o snd_driver_vm_rootfs.cpio.gz \
								--startup $(LINUX_TOOLS_DIR)/snd/sound \
								--home $(SND_DRIVER_VM_USERLEVEL_IMAGES) \
								--etc $(SND_DRIVER_VM_ASOUND_CONFIG)

snd_driver_vmm.o: $(EXAMPLE_DIR)/snd_driver_vmm.c $(CHECK_FLAGS_BOARD_MD5)
	$(CC) $(CFLAGS) -c -o $@ $<

snd_driver_vm_images.o: $(LIBVMM)/tools/package_guest_images.S $(SND_DRIVER_VM_DIR)/linux snd_driver_vm.dtb snd_driver_vm_rootfs.cpio.gz
	$(CC) -c -g3 -x assembler-with-cpp \
					-DGUEST_KERNEL_IMAGE_PATH=\"$(SND_DRIVER_VM_DIR)/linux\" \
					-DGUEST_DTB_IMAGE_PATH=\"snd_driver_vm.dtb\" \
					-DGUEST_INITRD_IMAGE_PATH=\"snd_driver_vm_rootfs.cpio.gz\" \
					-target $(TARGET) \
					$(LIBVMM)/tools/package_guest_images.S -o $@

snd_driver_vmm.elf: snd_driver_vmm.o snd_driver_vm_images.o
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

user_sound/%.o: $(SND_UIO_DRIVERS_DIR)/%.c
	mkdir -p user_sound
	$(CC_USERLEVEL) -c $(CFLAGS_USERLEVEL) $^ -o $@

user_sound.elf: user_sound/main.o user_sound/stream.o user_sound/queue.o user_sound/convert.o
	$(CC_USERLEVEL) $(CFLAGS_USERLEVEL) $^ -o $@
	patchelf --set-interpreter /lib64/ld-linux-aarch64.so.1 $@

%.elf: $(EXAMPLE_DIR)/userlevel/%.c
	$(CC_USERLEVEL) $(CFLAGS_USERLEVEL) $^ -o $@
	patchelf --set-interpreter /lib64/ld-linux-aarch64.so.1 $@


include $(SDDF)/util/util.mk
include $(UART_DRIVER)/uart_driver.mk
include $(SERIAL_COMPONENTS)/serial_components.mk
include $(SOUND_COMPONENTS)/sound_components.mk
include $(LIBVMM)/vmm.mk

qemu: $(IMAGE_FILE)
	if ! command -v $(QEMU) > /dev/null 2>&1; then echo "Could not find dependency: qemu-system-aarch64"; exit 1; fi
	$(QEMU) -machine virt,virtualization=on,secure=off \
			-cpu cortex-a53 \
			-serial mon:stdio \
			-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
			-audio driver=$(QEMU_SND_BACKEND),model=$(QEMU_SND_FRONTEND),id=$(QEMU_SND_BACKEND) \
			-m size=2G \
			-nographic

clean::
	$(RM) -f *.elf .depend* $
	find . -name \*.[do] |xargs --no-run-if-empty rm

clobber:: clean
	rm -f *.a
	rm -f $(IMAGE_FILE) $(REPORT_FILE)

-include benchmark.d
-include idle.d
-include native_client.d
-include snd_driver_vmm.d
