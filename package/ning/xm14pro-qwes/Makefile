# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-qwes
PKG_VERSION:=1.0
PKG_RELEASE:=18
PKG_LICENSE:=LicenseRef-Xiaomi-Qualcomm-Proprietary
PKG_FLAGS:=nonshared
RSTRIP:=:

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-qwes
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Xiaomi 14 Pro QWES secure modem license service
  DEPENDS:=+xm14pro-platform
endef

define Package/xm14pro-qwes/description
 Runs the stock Xiaomi 14 Pro qseecomd, ssgtzd and qwesd userspace closure
 needed by MPSS to validate the Smart Transmit license. It also restores the
 Qualcomm TOD base before QWES starts. The proprietary runtime files were
 extracted from this device's OS3.0.308.0 Android firmware.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/ssgtzd-launcher.c $(PKG_BUILD_DIR)/
	$(CP) ./src/android-log-bridge.c $(PKG_BUILD_DIR)/
	$(CP) ./src/secure-time.c $(PKG_BUILD_DIR)/
	$(CP) ./src/selinux-load.c $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -static \
		-o $(PKG_BUILD_DIR)/ssgtzd-launcher \
		$(PKG_BUILD_DIR)/ssgtzd-launcher.c
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -static \
		-o $(PKG_BUILD_DIR)/android-log-bridge \
		$(PKG_BUILD_DIR)/android-log-bridge.c
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -static \
		-o $(PKG_BUILD_DIR)/secure-time \
		$(PKG_BUILD_DIR)/secure-time.c
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -static \
		-o $(PKG_BUILD_DIR)/selinux-load \
		$(PKG_BUILD_DIR)/selinux-load.c
endef

define Package/xm14pro-qwes/install
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-qseecomd \
		$(1)/etc/init.d/xm14pro-qseecomd
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-qwes \
		$(1)/etc/init.d/xm14pro-qwes
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-secure-time \
		$(1)/etc/init.d/xm14pro-secure-time
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-selinux \
		$(1)/etc/init.d/xm14pro-selinux
	$(INSTALL_DIR) $(1)/usr/share/xm14pro-qwes
	$(INSTALL_DATA) ./files/usr/share/xm14pro-qwes/stock-sepolicy \
		$(1)/usr/share/xm14pro-qwes/stock-sepolicy
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-qwes
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ssgtzd-launcher \
		$(1)/usr/libexec/xm14pro-qwes/ssgtzd-launcher
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/android-log-bridge \
		$(1)/usr/libexec/xm14pro-qwes/android-log-bridge
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/secure-time \
		$(1)/usr/libexec/xm14pro-qwes/secure-time
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/selinux-load \
		$(1)/usr/libexec/xm14pro-qwes/selinux-load
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-qwes/wait-ready \
		$(1)/usr/libexec/xm14pro-qwes/wait-ready
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-qwes/time-keeper \
		$(1)/usr/libexec/xm14pro-qwes/time-keeper
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-qwes/android-root
	$(CP) ./files/android-root/. \
		$(1)/usr/libexec/xm14pro-qwes/android-root/
	$(INSTALL_DIR) \
		$(1)/usr/libexec/xm14pro-qwes/android-root/dev/socket \
		$(1)/usr/libexec/xm14pro-qwes/android-root/proc \
		$(1)/usr/libexec/xm14pro-qwes/android-root/sys \
		$(1)/usr/libexec/xm14pro-qwes/android-root/linkerconfig \
		$(1)/usr/libexec/xm14pro-qwes/android-root/data/misc/qsee \
		$(1)/usr/libexec/xm14pro-qwes/android-root/data/system/users \
		$(1)/usr/libexec/xm14pro-qwes/android-root/data/vendor/qwes \
		$(1)/usr/libexec/xm14pro-qwes/android-root/data/vendor/tzstorage \
		$(1)/usr/libexec/xm14pro-qwes/android-root/mnt/vendor/persist \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/firmware_mnt
endef

$(eval $(call BuildPackage,xm14pro-qwes))
