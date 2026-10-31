# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-nfc-ir-sidecar
PKG_VERSION:=1.0
PKG_RELEASE:=16
PKG_LICENSE:=LicenseRef-Xiaomi-Qualcomm-Proprietary
PKG_FLAGS:=nonshared

# Keep Xiaomi's Android ELF payloads byte-for-byte intact.
RSTRIP:=:

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-nfc-ir-sidecar
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Xiaomi 14 Pro stock NFC and infrared sidecars
  DEPENDS:=@TARGET_xm14pro_armv8 +xm14pro-telephony-sidecar +xm14pro-stock-modules +xm14pro-ir-database +xm14pro-ac-encoder
endef

define Package/xm14pro-nfc-ir-sidecar/description
 Runs the unmodified Xiaomi SN220T NFC AIDL HAL and Qualcomm consumer-IR
 service inside the existing Android/Bionic compatibility root. A narrow
 launcher supplies only the Android init identity, SELinux context and
 verified Binder/property compatibility environment.
endef

define Build/Prepare
	$(call Build/Prepare/Default)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) \
		-Wall -Wextra -Werror -O2 \
		-o $(PKG_BUILD_DIR)/xm14pro-peripheral-launcher \
		$(PKG_BUILD_DIR)/peripheral-launcher.c \
		$(TARGET_LDFLAGS)
endef

define Package/xm14pro-nfc-ir-sidecar/conffiles
/etc/config/xm14peripherals
endef

define Package/xm14pro-nfc-ir-sidecar/install
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-qwes/android-root
	$(CP) ./files/android-root/. \
		$(1)/usr/libexec/xm14pro-qwes/android-root/
	$(INSTALL_BIN) \
		./files/android-root/vendor/bin/xm14pro-peripheral-control \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/bin/xm14pro-peripheral-control
	$(INSTALL_BIN) \
		./files/android-root/vendor/bin/xm14pro-nfc-stack-probe \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/bin/xm14pro-nfc-stack-probe
	$(INSTALL_BIN) \
		./files/android-root/vendor/bin/xm14pro-nfc-service \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/bin/xm14pro-nfc-service
	ln -sf consumerir.qcom.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/odm/lib64/hw/consumerir.default.so
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-peripherals
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-peripheral-launcher \
		$(1)/usr/libexec/xm14pro-peripherals/launcher
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-peripherals/apply-stock-permissions \
		$(1)/usr/libexec/xm14pro-peripherals/apply-stock-permissions
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-peripherals/status \
		$(1)/usr/libexec/xm14pro-peripherals/status
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-peripherals/control \
		$(1)/usr/libexec/xm14pro-peripherals/control
	$(INSTALL_DIR) $(1)/usr/share/doc/xm14pro-nfc
	$(INSTALL_DATA) ./files/usr/share/doc/xm14pro-nfc/MCT-COMPATIBILITY.md \
		$(1)/usr/share/doc/xm14pro-nfc/MCT-COMPATIBILITY.md
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/xm14peripherals \
		$(1)/etc/config/xm14peripherals
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-nfc-ir-sidecar \
		$(1)/etc/init.d/xm14pro-nfc-ir-sidecar
endef

$(eval $(call BuildPackage,xm14pro-nfc-ir-sidecar))
