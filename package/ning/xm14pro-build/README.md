# Xiaomi 14 Pro userspace build layer

This directory prepares the `xm14pro/armv8` OpenWrt userspace for `shennong` while
the phone continues to use Xiaomi's exact 6.1.138 kernel, DTB/DTBO, vendor
ramdisk modules and firmware.

The generic OpenWrt 6.18 kernel produced inside this target is a build-time
side effect only. `postprocess.sh` does not export it and it must never be put
on the phone. Only the rootfs tar, the generated 8 GiB ext4 Android-sparse
staging image, and the separately built static boot shim are accepted into
`bin/targets/shennong/rootfs/`.

The menu selection is intentionally a single device:

```text
Target System:  Xiaomi 14 Pro nwrt CPE
Subtarget:      Xiaomi 14 Pro (shennong) ARM64 userspace
Target Profile: Xiaomi 14 Pro shennong nwrt CPE
```

Run from the repository root:

```sh
./package/ning/xm14pro-build/prepare.sh
make -j"$(nproc)" world
./package/ning/xm14pro-build/postprocess.sh
```

`prepare.sh` rejects a Xiaomi configuration containing an OpenWrt `kmod-*`
package. `postprocess.sh` creates an 8 GiB ext4 staging filesystem and then
calls `build-b-slot.sh`, which produces the production boot chain under
`bin/targets/shennong/boot-chain/` without executing `fastboot`, `dd`, changing
slots, or touching the phone.

The B-slot packer reuses exact-hash-gated stock A-slot kernel, vendor ramdisk,
DTB and DTBO inputs. Its second vendor ramdisk fragment is a bounded static
emergency shell, not another copy of the complete nwrt system. The complete
system is flashed only to the independent 200 GiB `linux_root` partition. It
is mounted directly as ext4 and grows from the 8 GiB staging size to the full
partition only after the runtime exact-size guard passes.

Android A-slot partitions and `userdata` are not part of the nwrt rootfs. The
B slot contains only the Xiaomi-compatible boot chain and bounded rescue
payload; generated manifests are marked `flashable=yes` only after validation.

