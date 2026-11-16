include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-medvemodem
# the engine's version (the owner, 2026-10-17): one number for both packages, tests/test_bench_notes.py checks it
PKG_VERSION:=0.16.0-alpha1
PKG_RELEASE:=1
PKG_LICENSE:=GPL-3.0-only
PKG_MAINTAINER:=Medvedolog
# po2lmo from LuCI's host tools: the pages' Russian (po/ru) becomes the catalog LuCI reads (the owner, 2026-10-23)
PKG_BUILD_DEPENDS:=luci-base/host

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-medvemodem
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=LuCI for the MedveModem modem manager
  PKGARCH:=all
  DEPENDS:=+luci-base +medvemodem
  # the engine of the same version only (the owner, 2026-10-17): the page and the ubus API it reads change together
  EXTRA_DEPENDS:=medvemodem (=$(PKG_VERSION)-r$(PKG_RELEASE))
endef

define Package/luci-app-medvemodem/description
 LuCI pages for the MedveModem engine, in English and Russian (LuCI's
 language picks one): the modems with their
 state and signal, a page per modem (network and signal, SMS and USSD,
 settings, diagnostics and recovery), the router's modem settings, and a
 line per modem on Status -> Overview. Reads and changes everything through
 the engine of the same version; no modem access of its own.
endef

define Build/Compile
	mkdir -p $(PKG_BUILD_DIR)/i18n
	$(foreach po,$(wildcard $(CURDIR)/po/*/*.po), \
		$(STAGING_DIR_HOSTPKG)/bin/po2lmo $(po) \
			$(PKG_BUILD_DIR)/i18n/$(basename $(notdir $(po))).$(notdir $(patsubst %/,%,$(dir $(po)))).lmo;)
endef

define Package/luci-app-medvemodem/install
	$(INSTALL_DIR) $(1)/www
	$(CP) ./htdocs/* $(1)/www/
	$(CP) ./root/* $(1)/
	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/i18n
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/i18n/*.lmo $(1)/usr/lib/lua/luci/i18n/
endef

# After an install or upgrade (the owner, 2026-10-25): the engine runs the new
# binary, rpcd is restarted rather than reloaded (its ACLs and rpcd plugins are
# read anew, and every LuCI session ends: the browser logs in again and loads
# the new pages), and LuCI's caches go.
define Package/luci-app-medvemodem/postinst
#!/bin/sh
if [ -z "$$IPKG_INSTROOT" ]; then
	/etc/init.d/medvemodemd enabled 2>/dev/null && /etc/init.d/medvemodemd restart >/dev/null 2>&1
	rm -f /tmp/luci-indexcache*
	rm -rf /tmp/luci-modulecache
	/etc/init.d/rpcd restart >/dev/null 2>&1 || true
fi
exit 0
endef

$(eval $(call BuildPackage,luci-app-medvemodem))
