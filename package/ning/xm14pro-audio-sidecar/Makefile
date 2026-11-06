# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-audio-sidecar
PKG_VERSION:=1.0
PKG_RELEASE:=15
PKG_LICENSE:=LicenseRef-Xiaomi-Qualcomm-Proprietary
PKG_FLAGS:=nonshared

# Preserve the stock Android ELF payload byte-for-byte. Android's linker uses
# section/version metadata which OpenWrt's normal sstrip pass would remove.
RSTRIP:=:

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-audio-sidecar
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Xiaomi 14 Pro stock PAL/AGM audio compatibility sidecar
  DEPENDS:=@TARGET_xm14pro_armv8 +xm14pro-telephony-sidecar +xm14pro-stock-modules
endef

define Package/xm14pro-audio-sidecar/description
 Runs Xiaomi's stock shennong Audio HAL, PAL/AGM stack and audio ADSP RPC
 daemon inside the existing Android/Bionic compatibility root. The stock
 routing XML, Forte ACDB and CS35L43 firmware are retained unchanged; a small
 nwrt launcher supplies only Android init identity, sockets and captured
 read-only properties.
endef

define Build/Prepare
	$(call Build/Prepare/Default)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) \
		-Wall -Wextra -Werror -O2 \
		-o $(PKG_BUILD_DIR)/xm14pro-audio-sidecar-launcher \
		$(PKG_BUILD_DIR)/audio-sidecar-launcher.c \
		$(TARGET_LDFLAGS)
	$(TARGET_CC) -nostdlib -shared -fPIC -fno-stack-protector \
		-mno-outline-atomics \
		-Wall -Wextra -Werror -O2 \
		-Wl,-soname,libxm14pro_audio_properties.so \
		-Wl,--allow-shlib-undefined \
		-Wl,--version-script=$(PKG_BUILD_DIR)/audio-properties.map \
		-o $(PKG_BUILD_DIR)/libxm14pro_audio_properties.so \
		$(PKG_BUILD_DIR)/audio-properties.c
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) \
		-Wall -Wextra -Werror -O2 \
		-o $(PKG_BUILD_DIR)/xm14pro-audio-control-client \
		$(PKG_BUILD_DIR)/audio-control-client.c \
		$(TARGET_LDFLAGS)
endef

define Package/xm14pro-audio-sidecar/conffiles
/etc/config/xm14audio
endef

define Package/xm14pro-audio-sidecar/install
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-qwes/android-root
	$(CP) ./files/android-root/. \
		$(1)/usr/libexec/xm14pro-qwes/android-root/
	$(INSTALL_DIR) \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/lib64
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/libxm14pro_audio_properties.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/lib64/libxm14pro_audio_properties.so
	# /lib is the immutable squashfs on the running nwrt image.  The kernel's
	# firmware_class path checks /usr/lib/firmware first, so keep the exact
	# stock CS35L43 payload there instead of attempting a runtime overlay.
	$(INSTALL_DIR) $(1)/usr/lib/firmware
	$(CP) ./files/lib/firmware/. $(1)/usr/lib/firmware/
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-audio
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-audio-sidecar-launcher \
		$(1)/usr/libexec/xm14pro-audio/launcher
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-audio/apply-stock-permissions \
		$(1)/usr/libexec/xm14pro-audio/apply-stock-permissions
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-audio/status \
		$(1)/usr/libexec/xm14pro-audio/status
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-audio/control \
		$(1)/usr/libexec/xm14pro-audio/control
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-audio-control-client \
		$(1)/usr/libexec/xm14pro-audio/control-client
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/xm14audio \
		$(1)/etc/config/xm14audio
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-audio-sidecar \
		$(1)/etc/init.d/xm14pro-audio-sidecar
endef

$(eval $(call BuildPackage,xm14pro-audio-sidecar))
