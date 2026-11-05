# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-cellular
PKG_VERSION:=1.0
PKG_RELEASE:=101
PKG_LICENSE:=GPL-2.0-only BSD-3-Clause

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-cellular
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Xiaomi 14 Pro internal Qualcomm cellular backend
  DEPENDS:=+modemmanager +modemmanager-rpcd \
	+qmi-utils +libqrtr-glib +jshn +jsonfilter +ubus +lua +ethtool +xm14pro-platform \
	+qcom-rmtfs +qcom-pd-mapper +qcom-tqftpserv +xm14pro-stock-modules \
	+xm14pro-qwes +xm14pro-nicm
endef

define Package/xm14pro-cellular/description
 Qualcomm QRTR/QMI bring-up and automatic mobile data connection support for
 the Xiaomi 14 Pro internal modem.
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) \
		-Wall -Wextra -Werror \
		-o $(PKG_BUILD_DIR)/xm14pro-call-monitor \
		./src/xm14pro-call-monitor.c $(TARGET_LDFLAGS)
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) \
		-Wall -Wextra -Werror \
		-o $(PKG_BUILD_DIR)/xm14pro-rmnet-ctl \
		./src/xm14pro-rmnet-ctl.c $(TARGET_LDFLAGS)
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) \
		-Wall -Wextra \
		-I$(STAGING_DIR)/usr/include/glib-2.0 \
		-I$(STAGING_DIR)/usr/lib/glib-2.0/include \
		-I$(STAGING_DIR)/usr/include/libqrtr-glib \
		-I$(STAGING_DIR)/usr/include/libqmi-glib \
		-o $(PKG_BUILD_DIR)/xm14pro-telephony \
		./src/xm14pro-telephony.c $(TARGET_LDFLAGS) \
		-lqmi-glib -lqrtr-glib -lgio-2.0 -lgobject-2.0 -lglib-2.0
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) \
		-Wall -Wextra -pthread \
		-o $(PKG_BUILD_DIR)/xm14pro-nv550-reader \
		./src/nv550/diag.c ./src/nv550/efs2.c ./src/nv550/util.c \
		./src/nv550/reader.c $(TARGET_LDFLAGS)
endef

define Package/xm14pro-cellular/conffiles
/etc/config/xm14cellular
endef

define Package/xm14pro-cellular/install
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/xm14cellular $(1)/etc/config/xm14cellular
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/xm14pro-cellular $(1)/etc/init.d/xm14pro-cellular
	$(INSTALL_DIR) $(1)/lib/udev/rules.d
	$(INSTALL_DATA) ./files/lib/udev/rules.d/78-mm-xm14pro-qcom-soc.rules \
		$(1)/lib/udev/rules.d/78-mm-xm14pro-qcom-soc.rules
	$(INSTALL_DIR) $(1)/etc/hotplug.d/net
	$(INSTALL_BIN) ./files/etc/hotplug.d/net/24-xm14pro-qmap \
		$(1)/etc/hotplug.d/net/24-xm14pro-qmap
	$(INSTALL_DIR) $(1)/lib/netifd/proto
	$(INSTALL_BIN) ./files/lib/netifd/proto/xm14pro.sh \
		$(1)/lib/netifd/proto/xm14pro.sh
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-modem
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-modem/autoconnect $(1)/usr/libexec/xm14pro-modem/autoconnect
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-modem/bringup $(1)/usr/libexec/xm14pro-modem/bringup
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-modem/control $(1)/usr/libexec/xm14pro-modem/control
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-modem/dsi-session $(1)/usr/libexec/xm14pro-modem/dsi-session
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-modem/resolve-apn $(1)/usr/libexec/xm14pro-modem/resolve-apn
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-modem/radio-status $(1)/usr/libexec/xm14pro-modem/radio-status
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-modem/telecom-status $(1)/usr/libexec/xm14pro-modem/telecom-status
	$(INSTALL_BIN) ./files/usr/libexec/xm14pro-modem/uim-keeper $(1)/usr/libexec/xm14pro-modem/uim-keeper
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-call-monitor $(1)/usr/sbin/xm14pro-call-monitor
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-rmnet-ctl $(1)/usr/sbin/xm14pro-rmnet-ctl
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-telephony $(1)/usr/sbin/xm14pro-telephony
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xm14pro-nv550-reader $(1)/usr/sbin/xm14pro-nv550-reader
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-qwes/android-root/vendor/bin
	$(INSTALL_BIN) ./files/android-root/vendor/bin/diag-router \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/bin/diag-router
	$(INSTALL_DIR) $(1)/usr/libexec/xm14pro-qwes/android-root/vendor/lib64
	# These two DIAG-specific libraries can share the stock vendor namespace.
	# Never install generic libbase/libcutils from a different Android build:
	# they override the radio sidecar's namespace and break vndservicemanager.
	$(INSTALL_DATA) ./files/android-root/vendor/lib64/libqsocket.so \
		./files/android-root/vendor/lib64/vendor.qti.diaghal@1.0.so \
		$(1)/usr/libexec/xm14pro-qwes/android-root/vendor/lib64/
	$(INSTALL_DIR) $(1)/usr/share/licenses/xm14pro-cellular
	$(INSTALL_DATA) ./src/nv550/LICENSE $(1)/usr/share/licenses/xm14pro-cellular/qcom-efs-browser
endef

$(eval $(call BuildPackage,xm14pro-cellular))
