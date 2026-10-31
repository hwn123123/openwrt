# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-nicm
PKG_VERSION:=1.0
PKG_RELEASE:=28
PKG_LICENSE:=LicenseRef-Xiaomi-Qualcomm-Proprietary
PKG_FLAGS:=nonshared
RSTRIP:=:

include $(INCLUDE_DIR)/package.mk

DSI_ANDROID_ROOT:=$(TOPDIR)/package/ning/xm14pro-qwes/files/android-root

define Package/xm14pro-nicm
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Xiaomi 14 Pro stock NICM data-plane runtime
  DEPENDS:=+xm14pro-qwes +xm14pro-telephony-sidecar
endef

define Package/xm14pro-nicm/description
 Installs the stock Xiaomi 14 Pro OS3.0.308.0 nicmd and its matching NICM/DSI
 runtime into the device-specific Android compatibility root.  This package
 does not modify generic OpenWrt or ModemManager behavior.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
endef

define Build/Compile
	$(TARGET_CC) -nostdlib -shared -fPIC -fno-stack-protector \
		-Wl,-soname,libxm14pro_nicm_compat.so \
		-o $(PKG_BUILD_DIR)/libxm14pro_nicm_compat.so \
		./src/nicm-platform-compat.c
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -static \
		-o $(PKG_BUILD_DIR)/nicmd-launcher \
		./src/nicmd-launcher.c
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -static \
		-o $(PKG_BUILD_DIR)/dsi-probe-launcher \
		./src/dsi-probe-launcher.c
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -static \
		-o $(PKG_BUILD_DIR)/dsi-dial-launcher \
		./src/dsi-dial-launcher.c
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -static \
		-o $(PKG_BUILD_DIR)/dds-control-launcher \
		./src/dds-control-launcher.c
	$(TARGET_CC) -ffreestanding -fno-builtin -fno-stack-protector -fPIE \
		-c -o $(PKG_BUILD_DIR)/android-start.o ./src/android-start.S
	$(TARGET_CC) -ffreestanding -fno-builtin -fno-stack-protector -fPIE \
		-c -o $(PKG_BUILD_DIR)/dsi-probe.o ./src/dsi-probe.c
	$(TARGET_CC) -ffreestanding -fno-builtin -fno-stack-protector -fPIE \
		-c -o $(PKG_BUILD_DIR)/dsi-dial.o ./src/dsi-dial.c
	$(TARGET_CC) -ffreestanding -fno-builtin -fno-stack-protector -fPIE \
		-c -o $(PKG_BUILD_DIR)/dds-control.o ./src/dds-control.c
	$(TARGET_CC) -nostdlib -pie \
		-Wl,-e,_start \
		-Wl,--dynamic-linker=/system/bin/bootstrap/linker64 \
		-Wl,--allow-shlib-undefined \
		-o $(PKG_BUILD_DIR)/xm14pro-dsi-probe \
		$(PKG_BUILD_DIR)/android-start.o \
		$(PKG_BUILD_DIR)/dsi-probe.o \
		$(DSI_ANDROID_ROOT)/vendor/lib64/libdsi_netctrl.so
	$(TARGET_CC) -nostdlib -pie \
		-Wl,-e,_start \
		-Wl,--dynamic-linker=/system/bin/bootstrap/linker64 \
		-Wl,--allow-shlib-undefined \
		-o $(PKG_BUILD_DIR)/xm14pro-dsi-dial \
		$(PKG_BUILD_DIR)/android-start.o \
		$(PKG_BUILD_DIR)/dsi-dial.o \
		$(DSI_ANDROID_ROOT)/vendor/lib64/libdsi_netctrl.so
	$(TARGET_CC) -nostdlib -pie \
		-Wl,-e,_start \
		-Wl,--dynamic-linker=/system/bin/bootstrap/linker64 \
		-Wl,--allow-shlib-undefined \
		-o $(PKG_BUILD_DIR)/xm14pro-dds-control \
		$(PKG_BUILD_DIR)/android-start.o \
		$(PKG_BUILD_DIR)/dds-control.o \
		$(DSI_ANDROID_ROOT)/vendor/lib64/libqmi_cci.so \
		$(DSI_ANDROID_ROOT)/vendor/lib64/libqmiservices.so
endef

define Package/xm14pro-nicm/install
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-qwes/android-root/vendor/bin
	$(INSTALL_BIN) ./files/android-root/vendor/bin/nicmd \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/bin/nicmd
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-dsi-probe \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/bin/xm14pro-dsi-probe
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-dsi-dial \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/bin/xm14pro-dsi-dial
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-dds-control \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/bin/xm14pro-dds-control
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-qwes/android-root/vendor/lib64
	$(CP) ./files/android-root/vendor/lib64/. \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/lib64/
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/libxm14pro_nicm_compat.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/lib64/libxm14pro_nicm_compat.so
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-qwes/android-root/vendor/etc/data
	$(INSTALL_DATA) ./files/android-root/vendor/etc/data/nicm_config.xml \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/etc/data/nicm_config.xml
	$(INSTALL_DATA) ./files/android-root/vendor/etc/data/dsi_config.xml \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/etc/data/dsi_config.xml
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-qwes/android-root/system/lib64
	$(INSTALL_DATA) ./files/android-root/system/lib64/libcom.android.tethering.connectivity_native.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/system/lib64/libcom.android.tethering.connectivity_native.so
	# The stock linker configuration resolves NICM's updatable connectivity
	# libraries through the com.android.tethering namespace, not /system/lib64.
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-qwes/android-root/apex/com.android.tethering/lib64
	$(INSTALL_DATA) ./files/android-root/apex/com.android.tethering/lib64/libcom.android.tethering.connectivity_native.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/apex/com.android.tethering/lib64/libcom.android.tethering.connectivity_native.so
	$(INSTALL_DATA) ./files/android-root/apex/com.android.tethering/lib64/libc++.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/apex/com.android.tethering/lib64/libc++.so
	# Qualcomm's DSI library opens its configuration below /system/vendor.
	# Android supplies /system/vendor -> /vendor; reproduce that standard
	# namespace relationship inside the device-specific compatibility root.
	$(LN) /vendor \
		$(1)/usr/libexec/xm14pro-qwes/android-root/system/vendor
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-nicm
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/nicmd-launcher \
		$(1)/usr/libexec/xm14pro-nicm/nicmd-launcher
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/dsi-probe-launcher \
		$(1)/usr/libexec/xm14pro-nicm/dsi-probe-launcher
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/dsi-dial-launcher \
		$(1)/usr/libexec/xm14pro-nicm/dsi-dial-launcher
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/dds-control-launcher \
		$(1)/usr/libexec/xm14pro-nicm/dds-control-launcher
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-nicm/dsi-dial \
		$(1)/usr/libexec/xm14pro-nicm/dsi-dial
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-nicm/modules
	$(INSTALL_DATA) ./files/usr/libexec/xm14pro-nicm/modules/tipc.ko \
		$(1)/usr/libexec/xm14pro-nicm/modules/tipc.ko
	$(INSTALL_DATA) ./files/usr/libexec/xm14pro-nicm/modules/tipc_diag.ko \
		$(1)/usr/libexec/xm14pro-nicm/modules/tipc_diag.ko
	$(INSTALL_DATA) ./files/usr/libexec/xm14pro-nicm/modules/ORIGIN.txt \
		$(1)/usr/libexec/xm14pro-nicm/modules/ORIGIN.txt
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-nicm/link-check \
		$(1)/usr/libexec/xm14pro-nicm/link-check
	$(INSTALL_DIR) $(1)/etc/config $(1)/etc/init.d
	$(INSTALL_CONF) ./files/etc/config/xm14nicm $(1)/etc/config/xm14nicm
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-nicm \
		$(1)/etc/init.d/xm14pro-nicm
endef

define Package/xm14pro-nicm/conffiles
/etc/config/xm14nicm
endef

$(eval $(call BuildPackage,xm14pro-nicm))
