include $(TOPDIR)/rules.mk

PKG_NAME:=medvemodem-bot
# the engine's version, as luci-app-medvemodem (tests/test_bench_notes.py checks it): no path
# arithmetic, which the SDK's package scan does not resolve like a build
PKG_VERSION:=0.16.0_alpha1
PKG_RELEASE:=12
PKG_LICENSE:=GPL-3.0-only
PKG_MAINTAINER:=Medvedolog

include $(INCLUDE_DIR)/package.mk

define Package/medvemodem-bot
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=WWAN
  TITLE:=Telegram bot for MedveModem
  PKGARCH:=all
  DEPENDS:=+medvemodem +ucode +ucode-mod-ubus +ucode-mod-uloop +ucode-mod-fs +ucode-mod-uci +ucode-mod-log +ucode-mod-digest +curl +ca-bundle +jq
  EXTRA_DEPENDS:=medvemodem (>=$(PKG_VERSION)-r$(PKG_RELEASE))
endef

define Build/Compile
endef

define Package/medvemodem-bot/conffiles
/etc/config/medvemodem-bot
/etc/config/medvemodem-bot-secret
/etc/medvemodem-bot/salt
endef

define Package/medvemodem-bot/install
	$(INSTALL_DIR) $(1)/etc/config $(1)/etc/init.d
	$(INSTALL_CONF) ./files/etc/config/medvemodem-bot $(1)/etc/config/medvemodem-bot
	$(INSTALL_BIN) ./files/etc/init.d/medvemodem-bot $(1)/etc/init.d/medvemodem-bot
	$(INSTALL_DIR) $(1)/usr/share/medvemodem-bot
	$(INSTALL_DATA) ./files/usr/share/medvemodem-bot/*.uc $(1)/usr/share/medvemodem-bot/
	echo '$(PKG_VERSION)-r$(PKG_RELEASE)' > $(1)/usr/share/medvemodem-bot/version
	$(INSTALL_DIR) $(1)/usr/libexec/rpcd $(1)/usr/libexec/medvemodem-bot
	$(INSTALL_BIN) ./files/usr/libexec/rpcd/medvemodem.bot.config $(1)/usr/libexec/rpcd/medvemodem.bot.config
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem-bot/init-secrets $(1)/usr/libexec/medvemodem-bot/init-secrets
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem-bot/why $(1)/usr/libexec/medvemodem-bot/why
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem-bot/netinfo $(1)/usr/libexec/medvemodem-bot/netinfo
	$(INSTALL_DIR) $(1)/etc/medvemodem-bot
	# the token and the salt are made on the router: sysupgrade keeps them (as the bench feed's package does)
	$(INSTALL_DIR) $(1)/lib/upgrade/keep.d
	printf '%s\n' /etc/config/medvemodem-bot-secret /etc/medvemodem-bot/salt > $(1)/lib/upgrade/keep.d/medvemodem-bot
endef

define Package/medvemodem-bot/description
 A Telegram bot for the MedveModem service: notices of sessions, quotas,
 outages and the main link, the state of the modems on request, and, when the
 owner allows it, modem control, SMS and USSD, with a keyboard and buttons. It runs as root like the engine and never touches
 ubusd; the token and the proxy passwords stay on the router
 (/etc/config/medvemodem-bot-secret, root 0600). TLS is always verified.
endef

define Package/medvemodem-bot/postinst
#!/bin/sh
[ -n "$$IPKG_INSTROOT" ] && exit 0
/usr/libexec/medvemodem-bot/init-secrets || exit 1
# the bot runs as root since r127: no ubusd ACL and never a signal to ubusd (r124: its reload took the bus from rpcd
# and LuCI). The mark of the old ACL goes; init-secrets above gave the files back to root
rm -f /etc/medvemodem-bot/acl.sha256
/etc/init.d/rpcd reload >/dev/null 2>&1 || true
# the new config file's reload trigger starts the bot while the package is still being unpacked (no why yet:
# the owner's upgrade 2026-10-21, CI on 24.10): started again here, with every file in place
/etc/init.d/medvemodem-bot enabled 2>/dev/null && /etc/init.d/medvemodem-bot restart >/dev/null 2>&1
exit 0
endef

define Package/medvemodem-bot/postrm
#!/bin/sh
[ -n "$$IPKG_INSTROOT" ] && exit 0
/etc/init.d/rpcd reload >/dev/null 2>&1 || true
exit 0
endef

$(eval $(call BuildPackage,medvemodem-bot))
