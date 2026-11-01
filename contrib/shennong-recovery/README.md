# Shennong NWRT Recovery

This directory is the single maintained recovery source for the Xiaomi 14 Pro
dual-system layout.

## Supported layout

- Android and all Android user data remain on slot A.
- Slot B contains only the NWRT boot chain.
- `/dev/block/by-name/linux_root` is the exact 200 GiB persistent NWRT root.
- `recovery_a` and `recovery_b` use the same recovery image. Back up and
  read-back-check each partition when updating it.

Obsolete Kali, `nwrt_sys1/sys2`, `nwrt_meta`, `nwrt_data`, and `nwrt_share`
layouts are not supported by this source tree.

## Recovery guarantees

- authenticated recovery ADB plus `/nwrt/bin/nwrt-root` root maintenance;
- stock PixelOS fastbootd delegation for `boot-fastboot`;
- fixed-target A/B selection scripts, with an exact 200 GiB ext4 check for
  the NWRT root instead of the obsolete Kali readiness marker;
- exact `linux_root` GPT geometry checks;
- built-in `sgdisk`, e2fsprogs from the base recovery, and a static Android
  sparse-image expander;
- no automatic formatting of `userdata` or `linux_root`.

## Build

This repository contains the NWRT integration and patch source. The 100 MiB
PixelOS base image and 100 MiB legacy selector input are binary dependencies,
not source code, and are not published here. The selector UI itself is a
precompiled binary. Put the following existing inputs beside `build.sh` before
building; the builder checks their exact SHA-256 values:

| Input | SHA-256 |
| --- | --- |
| `base-recovery.img` | `0d32c95a88f1ff626d40df4fc55071f86943662ccea84c9f165c659483565366` |
| `selector-base.img` | `944cd9b5f64a9c9d4c0f92bec8d355eafe4d67dd72c54f69759ce8414d845460` |
| `assets/adb_keys-host.pub` | `740ed6ae14efe79546ed76b7b361d9fb3e36aac1b00e6af24d9699d8f8829f3a` |

Run on the ARM64 Ubuntu build VM:

```sh
./build.sh
```

The builder pins every binary input by SHA-256, reopens the finished ramdisk,
checks the maintenance paths, verifies AVB, and writes `output/SHA256SUMS`.

The selector base also contains the firmware verifier, Android `init_boot`
root helper, slot-status helper, and their Magisk runtime.  They remain because
the visible recovery menus call those exact paths; they are not legacy build
debris.

## Feature status on the current 200 GiB layout

- Android slot A is already rooted with Magisk. Its `su` returns UID 0. The
  recovery root installer has its required image, Magisk assets, and rescue
  partition, but has not been run against this already rooted device because
  it writes `init_boot_a`.
- The firmware menu is **not an operational updater**. Its old verifier looks
  for `nwrt_share`, which was only a package exchange volume, not a firmware
  target. The current layout has `linux_root` instead. `xm14-fwctl status`
  and `scan` return code 10; all write commands are locked. A replacement
  updater must accept packages from ADB or external storage and map signed
  images to the existing A-slot Android, B-slot NWRT, and A/B recovery
  partitions. Android dynamic partitions need their own OTA or super-image
  handling, and updating `linux_root` cannot read its input package from that
  same partition while overwriting it. Do not present the old menu as a
  working firmware installation feature.
- Recovery power commands use the init action bridge. The selector's old
  direct reboot syscall failed under the recovery policy and displayed a
  false error. The v2.8 selector skips that false error; the bridge clears
  the recovery boot request before bootloader and power-off actions.
  On the device, the recovery, bootloader, and power-off menu actions were
  exercised successfully after installing v2.8.

`tools/gki/generate_gki_certificate.py` remains because `mkbootimg.py` imports
it at startup.  GKI tests, sample keys, and unrelated certification utilities
are deliberately excluded.

The old B-only ZIP installer was removed. The maintained REC image is shared
by `recovery_a` and `recovery_b`; update both with separate backups and
read-back checks.
