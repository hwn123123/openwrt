# Xiaomi 14 Pro OpenWrt target

This package owns the `xm14pro/armv8` OpenWrt build target used to create the
nwrt userspace for Xiaomi 14 Pro (`shennong`). The target is installed into
`target/linux/xm14pro` by `install-target.sh` so no upstream target is edited.

The OpenWrt kernel built by the build system is never exported to the phone.
The B-slot packer combines the generated root filesystem with the exact stock
Xiaomi kernel, vendor ramdisk, DTB and DTBO instead.

The stock kernel has network, UTS and seccomp support but omits IPC, PID and
user namespaces. The target therefore excludes `procd-ujail`; services still
run under procd, without requesting namespace jails that this kernel cannot
create.
