# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/image.mk

# The stock Xiaomi 6.1 kernel enables EROFS LZ4 but deliberately disables
# CONFIG_EROFS_FS_ZIP_LZMA.  OpenWrt's generic host-kernel configuration would
# otherwise select LZMA for this userspace image, which the phone rejects.
# Keep this override local to the shennong target.
EROFSCOMP := lz4hc,12

define Device/Default
  KERNEL := kernel-bin
endef

define Device/xiaomi_14_pro
  DEVICE_VENDOR := Xiaomi
  DEVICE_MODEL := 14 Pro
  DEVICE_VARIANT := shennong nwrt CPE
  DEVICE_PACKAGES := xm14pro-boot-shim xm14pro-defaults xm14pro-usb-gadget \
	xm14pro-boot-health xm14pro-platform xm14pro-stock-modules xm14pro-dms-stub \
	xm14pro-telephony-sidecar xm14pro-audio-sidecar xm14pro-audio-webui \
	xm14pro-camera-kernel xm14pro-camera-webui \
	xm14pro-nfc-ir-sidecar xm14pro-peripherals-webui xm14pro-nicm xm14pro-cellular xm14pro-bonding \
	xm14pro-openclash-compat \
	qcom-rmtfs qcom-pd-mapper qcom-tqftpserv \
	xm14pro-cellular-webui xm14pro-battery xm14pro-display xm14pro-display-webui \
	alsa-utils evtest input-support video-support libdrm libdrm-tests
  SUPPORTED_DEVICES := xiaomi,shennong
  FILESYSTEMS := erofs
  IMAGES := rootfs.erofs
  IMAGE/rootfs.erofs := append-rootfs
endef
TARGET_DEVICES += xiaomi_14_pro

$(eval $(call BuildImage))
