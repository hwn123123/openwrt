# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-dms-stub
PKG_VERSION:=1.0
PKG_RELEASE:=2
PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-dms-stub
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Xiaomi 14 Pro local QMI DMS WLAN MAC service
  DEPENDS:=@TARGET_xm14pro +qcom-qrtr +libuci
endef

define Package/xm14pro-dms-stub/description
 Minimal local QRTR/QMI DMS service used while the modem remote processor is
 unavailable. It answers only the WLAN MAC request required by cnss2.
endef

define Build/Prepare
	$(call Build/Prepare/Default)
	$(CP) ./src/xm14pro-dms-stub.c $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) \
		-Wall -Wextra -Werror \
		-o $(PKG_BUILD_DIR)/xm14pro-dms-stub \
		$(PKG_BUILD_DIR)/xm14pro-dms-stub.c \
		$(TARGET_LDFLAGS) -lqrtr
endef

define Package/xm14pro-dms-stub/conffiles
/etc/config/xm14dms
endef

define Package/xm14pro-dms-stub/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-dms-stub \
		$(1)/usr/sbin/xm14pro-dms-stub
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-dms-stub \
		$(1)/etc/init.d/xm14pro-dms-stub
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/xm14dms $(1)/etc/config/xm14dms
endef

$(eval $(call BuildPackage,xm14pro-dms-stub))
