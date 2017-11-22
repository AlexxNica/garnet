# Guest

The `guest` app enables booting a guest operating system using the Zircon
hypervisor.

These instructions will guide you through creating minimal Zircon and Linux
guests. For instructions on building a more comprehensive linux guest system
see [Hypervisor Benchmarking](docs/benchmarking.md).

## Building a guest image

To run a guest using the hypervisor, you must create a bootfs image containing
the guest and use the `guest` app to launch it. This section will provide steps
for building a simple Zircon and Linux guest image.

**Note:** `guest` only supports the Zircon and Linux kernels.

On the host system, run the following from the Fuchsia root (ex: the same
directory that contains `.jiri_root`):
```
# Optional: Build Linux, an initial RAM disk, and an EXT2 file-system.
zircon/system/uapp/guest/scripts/mklinux.sh
zircon/system/uapp/guest/scripts/mksysroot.sh -ri

# Optional: Build a GPT disk image for Zircon guests.
zircon/system/uapp/guest/scripts/mkgpt.sh

# This will assemble all the boot images and start the bootserver. It will be
# ready to netboot once you see:
#
# [bootserver] listening on [::]33331
garnet/bin/guest/scripts/build.sh x86
```

### Zircon guest

After netbooting the target device, to run Zircon:
```
guest -r /boot/data/bootdata.bin /boot/data/zircon.bin
```

To run Zircon using a GPT disk image:
```
guest \
    -b /boot/data/zircon.gpt \
    -r /boot/data/bootdata.bin \
    /boot/data/zircon.bin
```

### Linux guest

After netbooting the target device, to run Linux using an initial RAM disk:
```
guest -r /boot/data/initrd /boot/data/bzImage
```

To run Linux using a **read-only** EXT2 root file-system:
```
guest \
    -b /boot/data/rootfs.ext2 \
    -c 'root=/dev/vda ro init=/init' \
    /boot/data/bzImage
```

To run Linux using a **writable** EXT2 root file-system:
```
cp /boot/data/rootfs.ext2 /boot/data/rootfs-rw.ext2

guest \
    -b /boot/data/rootfs-rw.ext2 \
    -c 'root=/dev/vda rw init=/init' \
    /boot/data/bzImage
```

Linux also supports an interactive graphical framebuffer:

```
guest \
    -g \
    -r /boot/data/initrd \
    -c 'console=tty0' \
    /boot/data/bzImage
```

This will cause the guest to gain control of the framebuffer. Keyboard HID
events will be passed through to the guest using a virito-input device
(automatically enabled when the GPU is enaled with `-g`). You can
toggle back to the host virtcon by pressing Alt+Esc.