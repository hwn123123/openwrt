# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=xm14pro-iptables
PKG_VERSION:=1.8.10
PKG_RELEASE:=2

PKG_SOURCE:=iptables-$(PKG_VERSION).tar.xz
PKG_SOURCE_URL:=https://netfilter.org/projects/iptables/files
PKG_HASH:=5cc255c189356e317d070755ce9371eb63a1b783c34498fb8c30264f3cc59c9c
PKG_BUILD_DIR:=$(BUILD_DIR)/iptables-$(PKG_VERSION)

PKG_FIXUP:=autoreconf
PKG_INSTALL:=1
PKG_BUILD_PARALLEL:=1
PKG_LICENSE:=GPL-2.0-only
PKG_LICENSE_FILES:=COPYING

include $(INCLUDE_DIR)/package.mk

define Build/Compile
	$(call Build/Compile/Default)
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) \
		-Wall -Wextra -Werror \
		-o $(PKG_BUILD_DIR)/xm14pro-iptables-wrapper \
		$(CURDIR)/src/xm14pro-iptables-wrapper.c
endef

define Package/xm14pro-iptables
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Firewall
  TITLE:=Xiaomi stock-kernel iptables legacy userspace
  DEPENDS:=@TARGET_xm14pro_armv8 +PACKAGE_libxtables:libxtables
  PROVIDES:=iptables ip6tables
endef

define Package/xm14pro-iptables/description
 Legacy iptables userspace built without OpenWrt kmod dependencies. Xiaomi's
 stock 6.1 kernel already provides the conntrack, filter and NAT features used
 by this package; no OpenWrt kernel module is installed.
endef

TARGET_CPPFLAGS += \
	-I$(PKG_BUILD_DIR)/include \
	-I$(LINUX_DIR)/user_headers/include

TARGET_CFLAGS += \
	-I$(PKG_BUILD_DIR)/include \
	-I$(LINUX_DIR)/user_headers/include \
	-DNO_LEGACY

CONFIGURE_ARGS += \
	--enable-shared \
	--disable-static \
	--disable-devel \
	--disable-nftables \
	--disable-connlabel \
	--with-kernel="$(LINUX_DIR)/user_headers" \
	--with-xtlibdir=/usr/lib/iptables \
	--with-xt-lock-name=/var/run/xtables.lock

MAKE_FLAGS += \
	$(TARGET_CONFIGURE_OPTS) \
	COPT_FLAGS="$(TARGET_CFLAGS)" \
	KERNEL_DIR="$(LINUX_DIR)/user_headers/" \
	PREFIX=/usr \
	KBUILD_OUTPUT="$(LINUX_DIR)"

define Package/xm14pro-iptables/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/usr/lib/iptables $(1)/usr/lib
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/sbin/xtables-legacy-multi $(1)/usr/sbin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-iptables-wrapper $(1)/usr/sbin/iptables
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libxtables.so.* $(1)/usr/lib/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libip4tc.so.* $(1)/usr/lib/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libip6tc.so.* $(1)/usr/lib/
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/iptables/*.so $(1)/usr/lib/iptables/
	# OpenClash may select OpenWrt's split userspace extension packages. Keep
	# their package and runtime logic untouched, and remove only files they own
	# from this Xiaomi stock-kernel bundle so apk never sees duplicate owners.
	$(if $(CONFIG_PACKAGE_libxtables),rm -f $(1)/usr/lib/libxtables.so.*)
	$(if $(CONFIG_PACKAGE_iptables-mod-tproxy),rm -f $(1)/usr/lib/iptables/libxt_TPROXY.so)
	$(if $(CONFIG_PACKAGE_iptables-mod-extra),rm -f \
		$(1)/usr/lib/iptables/libxt_addrtype.so \
		$(1)/usr/lib/iptables/libxt_cgroup.so \
		$(1)/usr/lib/iptables/libxt_owner.so \
		$(1)/usr/lib/iptables/libxt_pkttype.so \
		$(1)/usr/lib/iptables/libxt_quota.so)
	ln -sf xtables-legacy-multi $(1)/usr/sbin/iptables-restore
	ln -sf xtables-legacy-multi $(1)/usr/sbin/iptables-save
	ln -sf xtables-legacy-multi $(1)/usr/sbin/ip6tables
	ln -sf xtables-legacy-multi $(1)/usr/sbin/ip6tables-restore
	ln -sf xtables-legacy-multi $(1)/usr/sbin/ip6tables-save
endef

$(eval $(call BuildPackage,xm14pro-iptables))
