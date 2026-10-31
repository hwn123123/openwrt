# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-usb-sidecar
PKG_VERSION:=1.0
PKG_RELEASE:=3
PKG_LICENSE:=LicenseRef-Xiaomi-Qualcomm-Proprietary
PKG_FLAGS:=nonshared

# Keep the stock Android ELF files byte-for-byte identical. Android's
# linker validates metadata which OpenWrt's normal sstrip pass would remove.
RSTRIP:=:

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-usb-sidecar
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Xiaomi 14 Pro stock USB and Type-C compatibility sidecar
  DEPENDS:=@TARGET_xm14pro_armv8 +xm14pro-telephony-sidecar +xm14pro-usb-gadget
endef

define Package/xm14pro-usb-sidecar/description
 Runs the stock HyperOS IUsb service inside the existing Android/Bionic
 compatibility root and supplies the framework-side IUsbCallback owner which
 Android normally starts from UsbPortManager. The stock NCM composition remains
 in the dedicated gadget package; nwrt keeps interface addressing, firewall and
 tethering policy. The unsupported IUsbGadget NCM path is not started.
endef

define Build/Prepare
	$(call Build/Prepare/Default)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) \
		-Wall -Wextra -Werror -O2 \
		-o $(PKG_BUILD_DIR)/xm14pro-usb-sidecar-launcher \
		$(PKG_BUILD_DIR)/usb-sidecar-launcher.c \
		$(TARGET_LDFLAGS)
endef

define Package/xm14pro-usb-sidecar/conffiles
/etc/config/xm14usbcompat
endef

define Package/xm14pro-usb-sidecar/install
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-qwes/android-root/vendor/bin/hw
	$(INSTALL_BIN) \
		./files/android-root/vendor/bin/hw/android.hardware.usb-service.qti \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/bin/hw/android.hardware.usb-service.qti
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-qwes/android-root/vendor/lib64
	$(INSTALL_DATA) \
		./files/android-root/vendor/lib64/android.hardware.usb-V1-ndk.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/lib64/android.hardware.usb-V1-ndk.so
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-qwes/android-root/vendor/bin
	$(INSTALL_BIN) \
		./files/android-root/vendor/bin/xm14pro-usb-framework \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/bin/xm14pro-usb-framework
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-usb-sidecar
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-usb-sidecar-launcher \
		$(1)/usr/libexec/xm14pro-usb-sidecar/launcher
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-usb-sidecar/apply-stock-permissions \
		$(1)/usr/libexec/xm14pro-usb-sidecar/apply-stock-permissions
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/xm14usbcompat \
		$(1)/etc/config/xm14usbcompat
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-usb-sidecar \
		$(1)/etc/init.d/xm14pro-usb-sidecar
endef

$(eval $(call BuildPackage,xm14pro-usb-sidecar))
