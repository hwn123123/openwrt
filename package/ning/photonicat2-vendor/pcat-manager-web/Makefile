include $(TOPDIR)/rules.mk

PKG_NAME:=pcat-manager-web
PKG_VERSION:=2.0.0
PKG_RELEASE:=86

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/photonicat/pcat-manager-web.git
PKG_SOURCE_DATE:=2026-06-11
PKG_SOURCE_VERSION:=937c7ffedefce50e16e79d27de56b52099a508dc
PKG_MIRROR_HASH:=3a9731d7fb5cb3342563332020bbdae0db1ee5a53e7f4aa5d76a78af80f3e2d0

PKG_MAINTAINER:=Photonicat
PKG_LICENSE:=Proprietary
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/pcat-manager-web
  SECTION:=admin
  CATEGORY:=Administration
  TITLE:=Official Photonicat Web management interface
  URL:=https://github.com/photonicat/pcat-manager-web
  DEPENDS:=@TARGET_rockchip_armv8 \
	+pcat-manager +pcat2-display-mini +pcat-sidecar +lpac-fm350 +uhttpd +ttyd \
	+python3 +python3-flask +python3-flask-socketio \
	+python3-pyserial +python3-requests +python3-sqlite3 \
	+python3-pam \
	+ip-full +pciutils +curl +ca-bundle +adb \
	+block-mount +parted +smartmontools \
	+e2fsprogs +exfat-mkfs +dosfstools +mkf2fs +btrfs-progs \
	+samba4-server +rsync +sshfs
endef

define Package/pcat-manager-web/description
  Official Photonicat Flask frontend. It listens on port 80 and moves the
  native OpenWrt LuCI uhttpd listeners to ports 8080 and 8443.
endef

define Package/pcat-manager-web/conffiles
/etc/pcat-nas/config.json
/etc/pcat-manager-web-user.json
/etc/pc_modem.db
/etc/pcat-radio-control.json
endef

define Build/Compile
endef

# Vendor modules excluded from the installed service. The first four have no
# runtime entry point; pc_terminal.py is replaced by the existing ttyd service.
PCAT_WEB_UNUSED_PY:=dummy_hardware.py indent_convert.py pc_phone_client.py remember_token.py pc_terminal.py

define Package/pcat-manager-web/install
	$(INSTALL_DIR) $(1)/usr/share/pcat-manager-web
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/app/*.py \
		$(1)/usr/share/pcat-manager-web/
	$(RM) $(addprefix $(1)/usr/share/pcat-manager-web/,$(PCAT_WEB_UNUSED_PY))
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/app/pcat-manager-web-main.py \
		$(1)/usr/share/pcat-manager-web/pcat-manager-web-main.py
	$(CP) $(PKG_BUILD_DIR)/app/pcat_util \
		$(1)/usr/share/pcat-manager-web/
	$(CP) $(PKG_BUILD_DIR)/app/static \
		$(1)/usr/share/pcat-manager-web/
	$(RM) \
		$(1)/usr/share/pcat-manager-web/static/xterm.css \
		$(1)/usr/share/pcat-manager-web/static/xterm.min.js \
		$(1)/usr/share/pcat-manager-web/static/xterm-addon-fit.min.js
	$(INSTALL_DATA) ./files/vendor-dark.css \
		$(1)/usr/share/pcat-manager-web/static/vendor-dark.css
	$(INSTALL_DATA) ./files/vendor-telemetry.js \
		$(1)/usr/share/pcat-manager-web/static/vendor-telemetry.js
	$(INSTALL_DATA) ./files/vendor-extensions.js ./files/vendor-extensions.css \
		$(1)/usr/share/pcat-manager-web/static/
	$(INSTALL_DATA) ./files/apps.svg \
		$(1)/usr/share/pcat-manager-web/static/apps.svg
	$(INSTALL_DATA) ./files/vendor-modem.js \
		$(1)/usr/share/pcat-manager-web/static/vendor-modem.js
	$(INSTALL_DATA) ./files/vendor-settings.js \
		$(1)/usr/share/pcat-manager-web/static/vendor-settings.js
	$(INSTALL_DATA) ./files/vendor-wifi.js \
		$(1)/usr/share/pcat-manager-web/static/vendor-wifi.js
	$(INSTALL_DATA) ./files/vendor-wifi.css \
		$(1)/usr/share/pcat-manager-web/static/vendor-wifi.css
	$(INSTALL_DATA) ./files/vendor-sound-events.js \
		$(1)/usr/share/pcat-manager-web/static/vendor-sound-events.js
	$(INSTALL_DATA) ./files/pcat_sound_events.py \
		$(1)/usr/share/pcat-manager-web/pcat_sound_events.py
	$(CP) $(PKG_BUILD_DIR)/app/templates \
		$(1)/usr/share/pcat-manager-web/
	$(RM) $(1)/usr/share/pcat-manager-web/templates/*.orig
	$(INSTALL_DATA) ./files/sound_events.html \
		$(1)/usr/share/pcat-manager-web/templates/sound_events.html
	$(INSTALL_DATA) ./files/extensions.html \
		$(1)/usr/share/pcat-manager-web/templates/extensions.html
	$(INSTALL_DATA) ./files/settings.html \
		$(1)/usr/share/pcat-manager-web/templates/settings.html
	$(INSTALL_DATA) ./files/terminal.html \
		$(1)/usr/share/pcat-manager-web/templates/terminal.html
	$(INSTALL_DATA) ./files/pcat_wifi.py \
		$(1)/usr/share/pcat-manager-web/pcat_wifi.py
	$(INSTALL_DATA) ./files/pcat_dashboard.py \
		$(1)/usr/share/pcat-manager-web/pcat_dashboard.py
	$(INSTALL_DATA) ./files/pcat_modem_usb.py \
		$(1)/usr/share/pcat-manager-web/pcat_modem_usb.py
	$(INSTALL_DATA) ./files/pcat_radio.py \
		$(1)/usr/share/pcat-manager-web/pcat_radio.py
	$(INSTALL_DATA) ./files/pcat_esim.py \
		$(1)/usr/share/pcat-manager-web/pcat_esim.py
	$(INSTALL_DATA) ./files/pcat_extensions.py \
		$(1)/usr/share/pcat-manager-web/pcat_extensions.py
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_CONF) ./files/pcat-radio-control.json \
		$(1)/etc/pcat-radio-control.json
	$(INSTALL_DATA) ./files/pcat_nas.py \
		$(1)/usr/share/pcat-manager-web/pcat_nas.py
	$(INSTALL_DATA) ./files/nas.html ./files/_nas_*.html \
		$(1)/usr/share/pcat-manager-web/templates/
	$(INSTALL_DATA) ./files/public_share.html ./files/public_link_password.html \
		$(1)/usr/share/pcat-manager-web/templates/
	$(INSTALL_DATA) ./files/_notification_standalone.html \
		$(1)/usr/share/pcat-manager-web/templates/_notification_standalone.html
	$(INSTALL_DATA) ./files/nas.svg \
		$(1)/usr/share/pcat-manager-web/static/nas.svg
	$(INSTALL_DATA) ./files/pollbus.js \
		$(1)/usr/share/pcat-manager-web/static/pollbus.js
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/pcat-manager-web.init \
		$(1)/etc/init.d/pcat-manager-web
	$(INSTALL_BIN) ./files/pcat-modem-health.init \
		$(1)/etc/init.d/pcat-modem-health
	$(INSTALL_BIN) ./files/pcat-usb-hub-recover.init \
		$(1)/etc/init.d/pcat-usb-hub-recover
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) ./files/pcat-terminal-shell \
		$(1)/usr/bin/pcat-terminal-shell
	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./files/99-pcat-manager-web \
		$(1)/etc/uci-defaults/99-pcat-manager-web
	$(INSTALL_BIN) ./files/99-pcat-terminal \
		$(1)/etc/uci-defaults/99-pcat-terminal
	$(INSTALL_DIR) $(1)/lib/upgrade/keep.d
	$(INSTALL_DATA) ./files/pcat-manager-web.keep \
		$(1)/lib/upgrade/keep.d/pcat-manager-web
endef

$(eval $(call BuildPackage,pcat-manager-web))
