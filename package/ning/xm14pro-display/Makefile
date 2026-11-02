# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-display
PKG_VERSION:=1.0.0
PKG_RELEASE:=65
PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-display
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Xiaomi 14 Pro native nwrt screen
  DEPENDS:=@TARGET_xm14pro +libdrm +libqrencode +jsonfilter +jshn +uci +curl +xm14pro-bonding
endef

define Package/xm14pro-display/description
 Native full-resolution DRM/KMS dashboard for the Xiaomi 14 Pro.
 The screen shows only live kernel and nwrt service data.
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -Wall -Wextra -Werror \
		-I$(STAGING_DIR)/usr/include/libdrm \
		-o $(PKG_BUILD_DIR)/xm14pro-screen ./src/xm14pro-screen.c \
		$(TARGET_LDFLAGS) -ldrm -lqrencode -lm
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -static \
		-Wall -Wextra -Werror -ffunction-sections -fdata-sections \
		-I$(STAGING_DIR)/usr/include/libdrm \
		-Wl,--gc-sections \
		-o $(PKG_BUILD_DIR)/xm14pro-early-splash \
		./src/xm14pro-early-splash.c \
		$(BUILD_DIR)/libdrm-*/ipkg-install/usr/lib/libdrm.a -lm
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -Wall -Wextra -Werror \
		-o $(PKG_BUILD_DIR)/xm14pro-touch-nav ./src/xm14pro-touch-nav.c $(TARGET_LDFLAGS)
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -Wall -Wextra -Werror \
		-o $(PKG_BUILD_DIR)/xm14pro-keys ./src/xm14pro-keys.c $(TARGET_LDFLAGS)
endef

define Package/xm14pro-display/conffiles
/etc/config/xm14display
endef

define Package/xm14pro-display/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] && exit 0
/etc/init.d/kmscon stop >/dev/null 2>&1 || true
/etc/init.d/kmscon disable >/dev/null 2>&1 || true
/etc/init.d/xm14pro-display enable >/dev/null 2>&1 || true
/etc/init.d/xm14pro-dashboard-ready enable >/dev/null 2>&1 || true
exit 0
endef

define Package/xm14pro-display/install
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/xm14display $(1)/etc/config/xm14display
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-display $(1)/etc/init.d/xm14pro-display
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-dashboard-ready $(1)/etc/init.d/xm14pro-dashboard-ready
	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./files/etc/uci-defaults/96-xm14pro-display $(1)/etc/uci-defaults/96-xm14pro-display
	$(INSTALL_DIR) $(1)/usr/libexec
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-display $(1)/usr/libexec/xm14pro-display
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-screen-data $(1)/usr/libexec/xm14pro-screen-data
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-speedtest $(1)/usr/libexec/xm14pro-speedtest
	$(INSTALL_DIR) $(1)/usr/libexec/xm14
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-early-splash $(1)/usr/libexec/xm14/early-splash
	$(INSTALL_DIR) $(1)/etc/xm14pro-speedtest
	$(INSTALL_CONF) ./files/etc/xm14pro-speedtest/servers.conf $(1)/etc/xm14pro-speedtest/servers.conf
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-screen $(1)/usr/sbin/xm14pro-screen
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-touch-nav $(1)/usr/sbin/xm14pro-touch-nav
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-keys $(1)/usr/sbin/xm14pro-keys
endef

$(eval $(call BuildPackage,xm14pro-display))

