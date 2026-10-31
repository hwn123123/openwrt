# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-peripherals-webui
PKG_VERSION:=1.0.0
PKG_RELEASE:=10
PKG_LICENSE:=GPL-2.0-only

LUCI_NAME:=luci-app-xm14pro-peripherals
LUCI_TYPE:=app
LUCI_BASENAME:=xm14pro-peripherals
LUCI_TITLE:=Xiaomi NFC and infrared manager
LUCI_DESCRIPTION:=LuCI controls for the Xiaomi 14 Pro stock NFC and consumer infrared compatibility services.
LUCI_DEPENDS:=+xm14pro-nfc-ir-sidecar +luci-base +rpcd +uci
LUCI_PKGARCH:=all
LUCI_MAINTAINER:=ning
LUCI_MINIFY_CSS:=0

define Package/xm14pro-peripherals-webui/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] && exit 0
# Reload rpcd so sessions created after an upgrade receive this application's
# file-exec ACL. Existing browser sessions will be asked to sign in again.
/etc/init.d/rpcd restart >/dev/null 2>&1 || true
exit 0
endef

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot package scanner signature
