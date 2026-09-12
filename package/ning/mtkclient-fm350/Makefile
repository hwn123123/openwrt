include $(TOPDIR)/rules.mk

PKG_NAME:=mtkclient-fm350
PKG_VERSION:=2.1.4
PKG_RELEASE:=8

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/bkerler/mtkclient.git
PKG_SOURCE_DATE:=2026-09-01
PKG_SOURCE_VERSION:=60e07f3b343a4469389f15967626d63e049968d4
PKG_MIRROR_HASH:=skip

PKG_LICENSE:=GPL-3.0-only
PKG_LICENSE_FILES:=LICENSE
PKG_MAINTAINER:=ning
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/mtkclient-fm350
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Minimal patched MTKClient runtime for FM350-GL
  URL:=https://github.com/bkerler/mtkclient
  DEPENDS:=+python3 +python3-pyusb +python3-pyserial \
	+python3-cryptodomex +python3-colorama +libusb-1.0
endef

define Package/mtkclient-fm350/description
 A trimmed command-line MTKClient runtime carrying the stock XML DA fixes
 proven by the Photonicat 2 FM350-GL backup and verified partition flashing
 workflow. Generic DA loader collections and the desktop GUI are omitted.
endef

define Build/Compile
endef

define Package/mtkclient-fm350/install
	$(INSTALL_DIR) $(1)/usr/lib/mtkclient/mtkclient
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/mtk.py $(1)/usr/lib/mtkclient/mtk.py
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/mtkclient/__init__.py \
		$(1)/usr/lib/mtkclient/mtkclient/__init__.py
	$(CP) $(PKG_BUILD_DIR)/mtkclient/Library \
		$(1)/usr/lib/mtkclient/mtkclient/
	$(CP) $(PKG_BUILD_DIR)/mtkclient/config \
		$(1)/usr/lib/mtkclient/mtkclient/
	$(CP) $(PKG_BUILD_DIR)/mtkclient/payloads \
		$(1)/usr/lib/mtkclient/mtkclient/
	$(RM) $(1)/usr/lib/mtkclient/mtkclient/Library/asmtools.py
	$(RM) $(1)/usr/lib/mtkclient/mtkclient/Library/DA/mtk_da_handler.py.before-fm350-erase
	find $(1)/usr/lib/mtkclient -type d -name __pycache__ -prune -exec rm -rf {} +
	find $(1)/usr/lib/mtkclient -type f -name '*.pyc' -delete
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/LICENSE $(1)/usr/lib/mtkclient/LICENSE
	printf '%s\n' '60e07f3b343a4469389f15967626d63e049968d4' \
		> $(1)/usr/lib/mtkclient/REVISION
endef

$(eval $(call BuildPackage,mtkclient-fm350))
