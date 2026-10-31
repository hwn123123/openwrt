# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

BOARD:=xm14pro
BOARDNAME:=Xiaomi 14 Pro nwrt CPE
FEATURES:=fpu rtc usb usbgadget rootfs-part erofs targz
KERNEL_PATCHVER:=6.18
DEVICE_TYPE:=basic

include $(INCLUDE_DIR)/target.mk

# Xiaomi's stock Android kernel deliberately omits IPC/PID/user namespaces.
# OpenWrt otherwise adds procd-ujail automatically on non-small-flash targets;
# keeping it would make jailed services such as dnsmasq fail at clone().  This
# target-local exclusion leaves the upstream defaults untouched.
DEFAULT_PACKAGES += -procd-ujail e2fsprogs resize2fs blkid block-mount

$(eval $(call BuildTarget))
