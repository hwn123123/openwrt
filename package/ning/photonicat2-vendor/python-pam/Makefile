include $(TOPDIR)/rules.mk

PKG_NAME:=python-pam
PKG_VERSION:=2.0.2
PKG_RELEASE:=1

PYPI_NAME:=python-pam
PKG_HASH:=97235235ba9b82dbae8068d1099508455949b275f77273ca22fdbd8b1fb5d950

# python-pam is a pure-Python ctypes module.  Install its source tree directly
# instead of invoking the generic PEP 517 wheel builder.
PYTHON3_PKG_BUILD:=0

PKG_MAINTAINER:=Photonicat
PKG_LICENSE:=MIT
PKG_LICENSE_FILES:=LICENSE
PKGARCH:=all

include $(TOPDIR)/feeds/packages/lang/python/pypi.mk
include $(INCLUDE_DIR)/package.mk
include $(TOPDIR)/feeds/packages/lang/python/python3-package.mk

define Package/python3-pam
  SECTION:=lang
  CATEGORY:=Languages
  SUBMENU:=Python
  TITLE:=Python PAM bindings using ctypes
  URL:=https://github.com/FirefighterBlu3/python-pam
  DEPENDS:=+python3-ctypes +python3-six +libpam
endef

define Package/python3-pam/description
  Pure-Python ctypes bindings used by the official Photonicat Web login.
endef

define Build/Compile
endef

define Package/python3-pam/install
	$(INSTALL_DIR) $(1)$(PYTHON3_PKG_DIR)
	$(CP) $(PKG_BUILD_DIR)/src/pam $(1)$(PYTHON3_PKG_DIR)/
endef

$(eval $(call BuildPackage,python3-pam))
