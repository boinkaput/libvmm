make -B MICROKIT_SDK=/home/ericc/tools/microkit-sdk-1.2.6 BOARD=odroidc4
scp -i/home/ericc/.ssh/id_ed25519 build/loader.img ericc@tftp:/tftpboot/odroidc4-3/virtio_blk.img