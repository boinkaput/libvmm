# A VMM for running the Hermit Operating System

## Building and running

```sh
make BOARD=<BOARD> MICROKIT_SDK=/path/to/sdk
```

Where `<BOARD>` is one of:
* `qemu_arm_virt`

Other configuration options can be passed to the Makefile such as `CONFIG`
and `BUILD_DIR`, see the Makefile for details.

If you would like to simulate the QEMU board you can run the following command:
```sh
make BOARD=qemu_arm_virt MICROKIT_SDK=/path/to/sdk qemu
```

