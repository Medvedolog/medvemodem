include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-medvemodem-bot
# the engine's version, as luci-app-medvemodem (tests/test_bench_notes.py checks it): no path
# arithmetic, which the SDK's package scan does not resolve like a build
PKG_VERSION:=0.16.0_alpha1
PKG_RELEASE:=1
PKG_LICENSE:=GPL-3.0-only
PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-medvemodem-bot
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=Staging LuCI panel for MedveModem Telegram bot
  PKGARCH:=all
  DEPENDS:=+luci-base +medvemodem-bot +luci-app-medvemodem
endef

define Build/Compile
endef

define Package/luci-app-medvemodem-bot/install
	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/medvemodem
	$(INSTALL_DATA) ./files/htdocs/luci-static/resources/view/medvemodem/bot.js $(1)/www/luci-static/resources/view/medvemodem/bot.js
	$(INSTALL_DATA) ./files/htdocs/luci-static/resources/view/medvemodem/bot-setup.js $(1)/www/luci-static/resources/view/medvemodem/bot-setup.js
	$(INSTALL_DATA) ./files/htdocs/luci-static/resources/view/medvemodem/bot-settings.js $(1)/www/luci-static/resources/view/medvemodem/bot-settings.js
	$(INSTALL_DIR) $(1)/www/luci-static/resources/medvemodem
	$(INSTALL_DATA) ./files/htdocs/luci-static/resources/medvemodem/botcommon.js $(1)/www/luci-static/resources/medvemodem/botcommon.js
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d $(1)/usr/share/rpcd/acl.d
	$(INSTALL_DATA) ./files/usr/share/luci/menu.d/luci-app-medvemodem-bot.json $(1)/usr/share/luci/menu.d/luci-app-medvemodem-bot.json
	$(INSTALL_DATA) ./files/usr/share/rpcd/acl.d/luci-app-medvemodem-bot.json $(1)/usr/share/rpcd/acl.d/luci-app-medvemodem-bot.json
endef

define Package/luci-app-medvemodem-bot/postinst
#!/bin/sh
if [ -z "$$IPKG_INSTROOT" ]; then
    rm -f /tmp/luci-indexcache
    rm -rf /tmp/luci-modulecache
    /etc/init.d/rpcd reload >/dev/null 2>&1 || true
fi
exit 0
endef

$(eval $(call BuildPackage,luci-app-medvemodem-bot))
