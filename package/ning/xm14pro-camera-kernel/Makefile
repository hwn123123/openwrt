# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-camera-kernel
PKG_VERSION:=1.3
PKG_RELEASE:=5
PKG_LICENSE:=LicenseRef-Xiaomi-Qualcomm-Proprietary
PKG_FLAGS:=nonshared
RSTRIP:=:

include $(INCLUDE_DIR)/package.mk

# Stock vendor modules reference drivers supplied by Xiaomi's early boot
# images. They are not OpenWrt library packages; keep the exception scoped to
# this unmodified binary-module package.
define CheckDependencies
endef

define Package/xm14pro-camera-kernel
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Xiaomi 14 Pro camera kernel adapter
  DEPENDS:=+xm14pro-stock-modules +libubox
endef

define Package/xm14pro-camera-kernel/description
 Stages the unmodified OS3.0.309.0 camera and EVA/CVP modules and their stock
 CAMERA_ICP, evass, and Shennong OIS firmware required by the camera pipeline. Capture
 also requires a working userspace provider.
endef

define Build/Compile
endef

define Package/xm14pro-camera-kernel/install
	$(INSTALL_DIR) $(1)/usr/lib/xm14pro/camera-modules
	$(CP) ./files/usr/lib/xm14pro/camera-modules/*.ko \
		$(1)/usr/lib/xm14pro/camera-modules/
	$(INSTALL_DIR) $(1)/usr/lib/firmware
	$(CP) ./files/usr/lib/firmware/CAMERA_ICP.* \
		$(1)/usr/lib/firmware/
	$(CP) ./files/usr/lib/firmware/evass.* \
		$(1)/usr/lib/firmware/
	$(CP) ./files/usr/lib/firmware/*ois*.prog \
		$(1)/usr/lib/firmware/
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-camera
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-camera/control \
		$(1)/usr/libexec/xm14pro-camera/control
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-camera/read-control \
		$(1)/usr/libexec/xm14pro-camera/read-control
endef

$(eval $(call BuildPackage,xm14pro-camera-kernel))
