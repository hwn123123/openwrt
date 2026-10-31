include $(TOPDIR)/rules.mk

PKG_NAME:=wireless-regdb
ifeq ($(CONFIG_TARGET_xm14pro),y)
  # Xiaomi's Android 6.1 kernel embeds only the sforshee regulatory key.
  # 2023.09.01 is the last official release signed by that trusted key.
  PKG_VERSION:=2023.09.01
  PKG_RELEASE:=1
  PKG_HASH:=26d4c2a727cc59239b84735aad856b7c7d0b04e30aa5c235c4f7f47f5f053491
else
  PKG_VERSION:=2026.09.03
  PKG_RELEASE:=1
  PKG_HASH:=b22e0901227b820cd1c280abe681a15b773a5103a5e10dc442e94ebb34cbf58d
endif
PKG_LICENSE:=ISC
PKG_LICENSE_FILES:=LICENSE

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.xz
PKG_SOURCE_URL:=@KERNEL/software/network/wireless-regdb/

PKG_MAINTAINER:=Felix Fietkau <nbd@nbd.name>

include $(INCLUDE_DIR)/package.mk

# The Xiaomi 14 Pro platform mounts the stock modem firmware partition
# read-only over /lib/firmware.  Its firmware_class search path deliberately
# includes /usr/lib/firmware for native NWRT firmware, so keep the regulatory
# database there instead of hiding it below the stock mount.
ifeq ($(CONFIG_TARGET_xm14pro),y)
  REGDB_INSTALL_DIR:=/usr/lib/firmware
  REGDB_INSTALL_FILES:=regulatory.db regulatory.db.p7s
else
  REGDB_INSTALL_DIR:=/lib/firmware
  REGDB_INSTALL_FILES:=regulatory.db
endif

define Package/wireless-regdb
  PKGARCH:=all
  SECTION:=firmware
  CATEGORY:=Firmware
  URL:=https://git.kernel.org/pub/scm/linux/kernel/git/wens/wireless-regdb.git/
  TITLE:=Wireless Regulatory Database
endef

define Build/Compile
	$(if $(CONFIG_TARGET_xm14pro),:,$(STAGING_DIR_HOST)/bin/$(PYTHON) $(PKG_BUILD_DIR)/db2fw.py $(PKG_BUILD_DIR)/regulatory.db $(PKG_BUILD_DIR)/db.txt)
endef

define Package/wireless-regdb/install
	$(INSTALL_DIR) $(1)$(REGDB_INSTALL_DIR)
	$(CP) $(addprefix $(PKG_BUILD_DIR)/,$(REGDB_INSTALL_FILES)) $(1)$(REGDB_INSTALL_DIR)/
endef

$(eval $(call BuildPackage,wireless-regdb))
