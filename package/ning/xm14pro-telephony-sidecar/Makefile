include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-telephony-sidecar
PKG_VERSION:=1.0
PKG_RELEASE:=73
PKG_LICENSE:=Proprietary
PKG_FLAGS:=nonshared

# The staged Android/Bionic executables and shared objects must retain their
# ELF metadata. OpenWrt's normal sstrip pass removes sections which Android's
# linker validates at startup. Keep every extracted Android binary byte-for-byte
# identical and implement nwrt integration only in the compatibility layer.
RSTRIP:=:

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-telephony-sidecar
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Xiaomi 14 Pro stock QCRIL and IMS compatibility sidecar
  DEPENDS:=+xm14pro-qwes +xm14pro-android-switch +qcom-qrtr
endef

define Package/xm14pro-telephony-sidecar/description
  Device-local Android compatibility sidecar for the stock Xiaomi 14 Pro
  QCRIL and IMS services, including Android's framework, hardware and vendor
  Binder service managers and the persistent radio state required during a
  full nwrt boot.
endef

define Build/Prepare
	$(call Build/Prepare/Default)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) -nostdlib -shared -fPIC -fno-stack-protector \
		-Wl,-soname,libxm14pro_telephony_compat.so \
		-Wl,--allow-shlib-undefined \
		-Wl,--version-script=$(PKG_BUILD_DIR)/telephony-compat.map \
		-o $(PKG_BUILD_DIR)/libxm14pro_telephony_compat.so \
		$(PKG_BUILD_DIR)/telephony-platform-compat.c
	# libbinder imports WaitForProperty@LIBBINDER, while libhidlbase imports
	# the same C++ symbol without a version.  Android's linker does not use the
	# versioned interposer for that unversioned relocation, so provide a second
	# narrowly scoped, unversioned view for libhidlbase.
	$(TARGET_CC) -nostdlib -shared -fPIC -fno-stack-protector \
		-Wl,-soname,libxm14pro_hidl_compat.so \
		-Wl,--allow-shlib-undefined \
		-o $(PKG_BUILD_DIR)/libxm14pro_hidl_compat.so \
		$(PKG_BUILD_DIR)/telephony-platform-compat.c
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) \
		-Wall -Wextra -O2 \
		-o $(PKG_BUILD_DIR)/xm14pro-telephony-launcher \
		$(PKG_BUILD_DIR)/telephony-launcher.c \
		$(TARGET_LDFLAGS)
endef

define Package/xm14pro-telephony-sidecar/conffiles
/etc/config/xm14telephony
endef

define Package/xm14pro-telephony-sidecar/install
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-qwes/android-root
	$(CP) ./files/android-root/. $(1)/usr/libexec/xm14pro-qwes/android-root/
	# QCRIL validates the selected hardware MBN against Android's writable
	# modem_config tree during every cold boot. Install the exact selected
	# stock manifest and payload there so the check does not fall back to the
	# slow self-repair path.
	$(INSTALL_DIR) \
		$(1)/usr/libexec/xm14pro-qwes/android-root/data/vendor/modem_config/mcfg_hw/generic/common/Lanai/STANDALONE/LA/DSDA_Hybrid
	$(INSTALL_DATA) \
		./files/android-root/readonly/firmware/image/modem_pr/mcfg/configs/mcfg_hw/oem_hw.txt \
		$(1)/usr/libexec/xm14pro-qwes/android-root/data/vendor/modem_config/mcfg_hw/oem_hw.txt
	$(INSTALL_DATA) \
		./files/android-root/readonly/firmware/image/modem_pr/mcfg/configs/mcfg_hw/generic/common/Lanai/STANDALONE/LA/DSDA_Hybrid/mcfg_hw.mbn \
		$(1)/usr/libexec/xm14pro-qwes/android-root/data/vendor/modem_config/mcfg_hw/generic/common/Lanai/STANDALONE/LA/DSDA_Hybrid/mcfg_hw.mbn
	# Software MBN selection depends on the SIM operator and must remain valid
	# after either card is replaced. Install the complete stock selection tree,
	# not a carrier-specific subset.
	$(INSTALL_DIR) \
		$(1)/usr/libexec/xm14pro-qwes/android-root/data/vendor/modem_config/mcfg_sw
	$(CP) \
		./files/android-root/readonly/firmware/image/modem_pr/mcfg/configs/mcfg_sw/. \
		$(1)/usr/libexec/xm14pro-qwes/android-root/data/vendor/modem_config/mcfg_sw/
	# The extracted VNDK APEX also contains RenderScript, graphics and media
	# libraries which are outside the telephony dependency closure.  Shipping
	# them would introduce Android EGL/Media dependencies into an nwrt package
	# even though none of the Binder, QCRIL, IMS or peripheral services loads
	# them. Keep only the compatibility payload required by this sidecar.
	$(RM) \
		$(1)/usr/libexec/xm14pro-qwes/android-root/apex/com.android.vndk.v34/lib64/libRSCpuRef.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/apex/com.android.vndk.v34/lib64/libRSDriver.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/apex/com.android.vndk.v34/lib64/libRS_internal.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/apex/com.android.vndk.v34/lib64/libbufferqueueconverter.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/apex/com.android.vndk.v34/lib64/libgui.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/apex/com.android.vndk.v34/lib64/libmedia_omx.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/apex/com.android.vndk.v34/lib64/libprocessgroup.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/apex/com.android.vndk.v34/lib64/libstagefright_bufferqueue_helper.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/apex/com.android.vndk.v34/lib64/libstagefright_omx.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/apex/com.android.vndk.v34/lib64/libstagefright_omx_utils.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/apex/com.android.vndk.v34/lib64/libstagefright_xmlparser.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/apex/com.android.vndk.v34/lib64/libui.so
	# Stock init selects manifest_pineapple.xml through ro.boot.product.vendor.sku.
	# The nwrt compatibility root intentionally has no Android property service,
	# so materialize that exact stock selection as the vendor main manifest.
	$(LN) manifest_pineapple.xml \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/etc/vintf/manifest.xml
	# HyperOS mounts system_ext at /system_ext and exposes /system/system_ext as
	# a link.  Keep the extracted source file in its archival location, but
	# install hwservicemanager at the path used by libvintf and the stock OS.
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-qwes/android-root/system_ext/bin
	$(INSTALL_BIN) ./files/android-root/system/system_ext/bin/hwservicemanager \
		$(1)/usr/libexec/xm14pro-qwes/android-root/system_ext/bin/hwservicemanager
	# libhidlbase checks this canonical path before it attempts any hwbinder
	# transaction.  HyperOS starts the binary from system_ext, so expose that
	# same binary at the path expected by the stock HIDL client library.
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-qwes/android-root/system/bin
	$(LN) /system_ext/bin/hwservicemanager \
		$(1)/usr/libexec/xm14pro-qwes/android-root/system/bin/hwservicemanager
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-telephony
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-telephony-launcher \
		$(1)/usr/libexec/xm14pro-telephony/launcher
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/libxm14pro_telephony_compat.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/lib64/libxm14pro_telephony_compat.so
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/libxm14pro_hidl_compat.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/lib64/libxm14pro_hidl_compat.so
	# SystemSuspend is a platform process and its linker namespace cannot load
	# an LD_PRELOAD object from /vendor. Install the same narrow HIDL readiness
	# bridge in the platform namespace; QCRIL keeps using the vendor copy above.
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/libxm14pro_hidl_compat.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/system/lib64/libxm14pro_hidl_compat.so
	$(INSTALL_DIR) $(1)/etc/init.d $(1)/etc/config
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-binder-managers \
		$(1)/etc/init.d/xm14pro-binder-managers
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-peripheral-services \
		$(1)/etc/init.d/xm14pro-peripheral-services
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-peripheral-proxy \
		$(1)/etc/init.d/xm14pro-peripheral-proxy
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-system-suspend \
		$(1)/etc/init.d/xm14pro-system-suspend
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-cnd \
		$(1)/etc/init.d/xm14pro-cnd
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-telephony-sidecar \
		$(1)/etc/init.d/xm14pro-telephony-sidecar
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-radio-framework \
		$(1)/etc/init.d/xm14pro-radio-framework
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-ims-client \
		$(1)/etc/init.d/xm14pro-ims-client
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-telephony/ims-bridge \
		$(1)/usr/libexec/xm14pro-telephony/ims-bridge
	$(INSTALL_CONF) ./files/etc/config/xm14telephony \
		$(1)/etc/config/xm14telephony
endef

$(eval $(call BuildPackage,xm14pro-telephony-sidecar))
