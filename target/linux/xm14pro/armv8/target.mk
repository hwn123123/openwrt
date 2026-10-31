ARCH:=aarch64
SUBTARGET:=armv8
BOARDNAME:=Xiaomi 14 Pro (shennong) ARM64 userspace
CPU_TYPE:=generic

define Target/Description
  Build the nwrt ARM64 userspace for Xiaomi 14 Pro (shennong). The generated
  OpenWrt kernel is a build-time artifact only; deployment uses the exact
  Xiaomi stock kernel, vendor ramdisk, DTB and DTBO.
endef
