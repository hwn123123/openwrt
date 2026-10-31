# SPDX-License-Identifier: MIT
include $(TOPDIR)/rules.mk

PKG_NAME:=xm14pro-ir-database
PKG_VERSION:=2026.04.24
PKG_RELEASE:=4
PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/flipperdevices/IRDB.git
PKG_SOURCE_VERSION:=f7b15366521cc81ba11b341538f0097bddbb748b
PKG_MIRROR_HASH:=e2fe8c017b0d89dd0c3b9c0755f20e9e7ffdd12f1bdb6a29834240a72f0337ea
PKG_LICENSE:=MIT
PKG_LICENSE_FILES:=LICENSE
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/xm14pro-ir-database
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Offline infrared remote database for Xiaomi 14 Pro
  DEPENDS:=@TARGET_xm14pro_armv8
endef

define Package/xm14pro-ir-database/description
 Installs an offline catalog of raw infrared remote-control signals. The
 catalog is organized by device type, brand and model and is used by the
 Xiaomi 14 Pro infrared manager and touchscreen interface.
endef

define Build/Compile
endef

define Package/xm14pro-ir-database/install
	$(INSTALL_DIR) $(1)/usr/share/xm14pro-ir
	cd $(PKG_BUILD_DIR)/database/categories && \
		find . -type f -name '*.ir' -exec sh -c ' \
			for file do \
				if grep -q "^type: raw$$$$" "$$$$file"; then \
					target="$(1)/usr/share/xm14pro-ir/$$$$file"; \
					mkdir -p "$$$${target%/*}"; \
					cp "$$$$file" "$$$$target"; \
				fi; \
			done \
		' sh {} +
	cd $(1)/usr/share/xm14pro-ir && \
		find . -type f -name '*.ir' | sort | while IFS= read -r file; do \
			relative="$$$${file#./}"; \
			category="$$$${relative%%/*}"; \
			remainder="$$$${relative#*/}"; \
			brand="$$$${remainder%%/*}"; \
			model="$$$${relative##*/}"; model="$$$${model%.ir}"; \
			buttons="$$$$(awk '\
				/^name: / { name=$$$$0; sub(/^name: /, "", name); type="" } \
				/^type: / { type=$$$$0; sub(/^type: /, "", type); if (type == "raw") count++ } \
				END { print count + 0 }' "$$$$file")"; \
			printf '%s|%s|%s|%s|%s\n' \
				"$$$$relative" "$$$$category" "$$$$brand" "$$$$model" "$$$$buttons"; \
		done > $(1)/usr/share/xm14pro-ir/catalog.tsv
	cd $(1)/usr/share/xm14pro-ir && { \
		printf '{"ok":true,"items":['; first=1; \
		while IFS='|' read -r relative category brand model buttons; do \
			[ "$$$$first" = 1 ] || printf ','; first=0; \
			printf '{"id":"%s","category":"%s","brand":"%s","model":"%s","buttons":%s}' \
				"$$$$relative" "$$$$category" "$$$$brand" "$$$$model" "$$$$buttons"; \
		done < catalog.tsv; \
		printf ']}\n'; \
	} > $(1)/usr/share/xm14pro-ir/catalog.json
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/LICENSE \
		$(1)/usr/share/xm14pro-ir/LICENSE
endef

$(eval $(call BuildPackage,xm14pro-ir-database))
