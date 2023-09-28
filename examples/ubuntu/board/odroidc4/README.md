## Getting the device tree from Linux user-space

* TODO: explain *why* we got the DTS from Linux user-space and not from U-Boot/Linux source code etc.

The following command was used:
```sh
dtc -I fs /sys/firmware/devicetree/base ubuntu-minimal.dts
```

This DTS is `board/odroidc4/ubuntu-minimal.dts`, it has not been modified at all. Instead, we apply the overlay
DTS in order to do virtualisation specific stuff.
