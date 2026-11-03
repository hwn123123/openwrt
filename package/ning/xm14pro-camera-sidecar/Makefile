include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-camera-sidecar
PKG_VERSION:=1.0
PKG_RELEASE:=2
PKG_LICENSE:=GPL-2.0-only
PKG_FLAGS:=nonshared

# Android/Bionic executables must keep their ELF sections for the stock linker.
RSTRIP:=:

include $(INCLUDE_DIR)/package.mk

# OpenWrt's package dependency scanner sees the Android NDK imports as missing
# musl libraries. They intentionally resolve inside the isolated stock Android
# root, never in the OpenWrt process namespace. Validate the ELF architecture
# and Android interpreter here instead of claiming nonexistent musl packages.
define CheckDependencies
	@$(TARGET_CROSS)readelf -h ./files/usr/libexec/xm14pro-camera/camera-snapshot | grep -q 'Machine:.*AArch64'
	@$(TARGET_CROSS)readelf -l ./files/usr/libexec/xm14pro-camera/camera-snapshot | grep -q '/system/bin/linker64'
	@$(TARGET_CROSS)readelf -h ./files/usr/libexec/xm14pro-camera/binderfs-add | grep -q 'Machine:.*AArch64'
endef

define Package/xm14pro-camera-sidecar
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Xiaomi 14 Pro isolated camera snapshot client
  DEPENDS:=+xm14pro-camera-kernel +xm14pro-telephony-sidecar
endef

define Package/xm14pro-camera-sidecar/description
  Android NDK camera client for the isolated stock camera provider. The
  original Android images are device-local read-only assets and are not
  redistributed in this package.
endef

define Build/Compile
	# The Android/Bionic client is built by src/build-snapshot.sh against the
	# stock system libraries. Do not relink it against musl here.
	test -s ./files/usr/libexec/xm14pro-camera/camera-snapshot
	echo '4e110478f6c3aa4c9a61040f277fa3bc854e6dd313c41590fd4a929fd056ab37  ./files/usr/libexec/xm14pro-camera/camera-snapshot' | sha256sum -c -
	echo '69504382f3a793c0b789cf7833d1b16443819d26c537debb0fad542ccba06366  ./files/usr/libexec/xm14pro-camera/binderfs-add' | sha256sum -c -
endef

define Package/xm14pro-camera-sidecar/install
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-camera
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-camera/camera-snapshot \
		$(1)/usr/libexec/xm14pro-camera/camera-snapshot
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-camera/setup-isolated-root \
		$(1)/usr/libexec/xm14pro-camera/setup-isolated-root
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-camera/binderfs-add \
		$(1)/usr/libexec/xm14pro-camera/binderfs-add
endef

$(eval $(call BuildPackage,xm14pro-camera-sidecar))
