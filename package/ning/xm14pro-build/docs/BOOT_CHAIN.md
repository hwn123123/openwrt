# B-slot boot-chain contract

The B slot is a boot loader for nwrt, not the nwrt system disk.

The nwrt display package and both nwrt startup scenes are B-slot/nwrt-rootfs
features only. They never write `boot_a`, `init_boot_a`, `vendor_boot_a`, the
Android system partitions, or Android's Xiaomi logo and boot animation.

`build-b-slot.sh` may produce only these B-slot artifacts:

- `boot_b`: exact Xiaomi-compatible kernel image;
- `init_boot_b`: the small static `xm14pro-boot-shim`, the fail-open native
  DRM startup animation, and minimum files;
- `vendor_boot_b`: exact stock vendor ramdisk/DTB plus a bounded rescue payload;
- `dtbo_b`: exact stock matching DTBO unless a reviewed device overlay is needed;
- `vbmeta_b`: controlled AVB metadata for the unlocked device.

It is a host-side packer, not a flasher. It contains no `fastboot` command, no
block-device `dd`, no slot switch and no partitioning operation.

The static PID 1 performs this sequence:

1. create a bounded static `/dev` set (the stock kernel has no `devtmpfs`) and
   mount `proc`, `sysfs` and `tmpfs`;
2. load the stock vendor `modules.load` and `modules.load.recovery` entries in
   dependency order so the UFS and DWC3 USB paths are both available;
3. create `/dev/block` and `/dev/block/by-name` from sysfs partition metadata;
4. resolve `/dev/block/by-name/linux_root` and mount it directly as ext4;
5. move the required stock firmware mounts into the persistent root and enter
   its `/sbin/init`;
6. enter the bounded rescue shell when `linux_root` cannot be mounted or lacks
   a usable init.

The CPE image does not contain a boot-chain health watchdog or install a
userspace health-decision service. A transient management-interface problem
must not reboot an otherwise running device. Kernel failures remain observable
through the stock pstore/mtdoops path and persistent boot logs.

The shim may mount only `linux_root` as the persistent system root. It must
never mount `userdata` as its system root and it never formats a partition.
The nwrt userspace may run `resize2fs` once, only after verifying that `/` is
the exact 200 GiB partition and not an Android partition.

The rescue fragment contains an exact-hash-gated static AArch64 BusyBox and a
minimal PID 1 script. It does not contain the full OpenWrt userspace, so adding
LuCI or Docker packages cannot overflow `vendor_boot_b`.

Every generated chain is checked for fixed partition sizes, AVB footers,
`vbmeta_b` verification flags, exact stock `boot_a`/`dtbo_a` hashes, a static
AArch64 shim, a valid ext4 sparse image and a configuration with zero OpenWrt
`kmod-*` packages.

The storage contract is one independent partition: partition 34 named
`linux_root`, exactly 200 GiB. Android remains in slot A; nwrt uses the B-slot
boot chain plus this partition.

