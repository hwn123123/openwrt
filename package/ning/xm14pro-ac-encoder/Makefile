# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-ac-encoder
PKG_VERSION:=2026.10.15
PKG_RELEASE:=2
PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/crankyoldgit/IRremoteESP8266.git
PKG_SOURCE_VERSION:=1e2f0f3ef0a93cbf2a8ddb2e95130f8f4c584b3f
PKG_MIRROR_HASH:=258ba49393c99408a4720561b206a1f3084507bcc5f38f0cc90a93a79809fb37
PKG_LICENSE:=LGPL-2.1-or-later
PKG_LICENSE_FILES:=LICENSE.txt
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-ac-encoder
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Stateful air-conditioner infrared encoder
  DEPENDS:=@TARGET_xm14pro_armv8 +libstdcpp
endef

define Package/xm14pro-ac-encoder/description
 Builds complete state frames for supported air-conditioner infrared protocols.
 The generated carrier and timing pattern are handed to the Xiaomi 14 Pro
 consumer-IR HAL by xm14pro-nfc-ir-sidecar.
endef

define Build/Prepare
	$(call Build/Prepare/Default)
	$(CP) ./src/xm14pro-ac-encode.cpp $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CXX) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) \
		-std=gnu++17 -DUNIT_TEST -DSWIGLIB -D_IR_LOCALE_=en-AU \
		-ffunction-sections -fdata-sections -I$(PKG_BUILD_DIR)/src \
		-I$(PKG_BUILD_DIR)/test \
		-o $(PKG_BUILD_DIR)/xm14pro-ac-encode \
		$(PKG_BUILD_DIR)/xm14pro-ac-encode.cpp \
		$(PKG_BUILD_DIR)/src/*.cpp \
		$(TARGET_LDFLAGS) -Wl,--gc-sections -pthread
endef

define Package/xm14pro-ac-encoder/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/usr/share/xm14pro-ac \
		$(1)/usr/share/licenses/xm14pro-ac-encoder
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-ac-encode $(1)/usr/bin/
	$(INSTALL_DATA) ./files/protocols.tsv $(1)/usr/share/xm14pro-ac/
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/LICENSE.txt \
		$(1)/usr/share/licenses/xm14pro-ac-encoder/LICENSE.txt
endef

$(eval $(call BuildPackage,xm14pro-ac-encoder))
