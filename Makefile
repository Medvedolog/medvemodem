include $(TOPDIR)/rules.mk

PKG_NAME:=medvemodem
PKG_VERSION:=0.16.0_alpha1
PKG_RELEASE:=2

# QModem's core, built from its source by CI for the LuCI installer
# (files/usr/libexec/medvemodem/qmodem-install);
# CI sets MEDVEMODEM_DOWNLOAD_BASE to the release that carries those packages
QMODEM_VERSION:=3.3.0_p20261108
QMODEM_COMMIT:=3196ab787e06f93ef343d0beceadd9758e78ef8b
QMODEM_ARCHES:=aarch64_cortex-a53 aarch64_generic x86_64
MEDVEMODEM_DOWNLOAD_BASE?=

PKG_LICENSE:=GPL-3.0-only
PKG_LICENSE_FILES:=LICENSE
PKG_MAINTAINER:=Medvedolog

# libubox, libubus and libuci are linked in from these commits (the host tests' pins, .github/scripts/host-ubus.sh):
# apk and ipk name them after their ABI date, so a build against an SDK's shared ones installs on that release only
# (a 25.12.5 build on a snapshot: «libubox20260213 (no such package)», the owner's Nokia, 2026-10-27). tools/static-libs.sh
MEDVEMODEM_LIBUBOX:=e7608b69283d919d031d13cc8e21692503f5dbea
MEDVEMODEM_UBUS:=414c60a2e29e72c2c4f573bc50ad54aef076d169
MEDVEMODEM_UCI:=74f6277aabffc943d026f406df57c22595134c42

include $(INCLUDE_DIR)/package.mk

# the commit pins the content; the packed tarball's hash depends on the release's packer (as medvemodem-lpac)
define Download/medvemodem-libubox
  PROTO:=git
  URL:=https://github.com/openwrt/libubox.git
  SOURCE_VERSION:=$(MEDVEMODEM_LIBUBOX)
  SUBDIR:=libubox-$(MEDVEMODEM_LIBUBOX)
  FILE:=medvemodem-libubox-$(MEDVEMODEM_LIBUBOX).tar.zst
  MIRROR_HASH:=skip
endef
$(eval $(call Download,medvemodem-libubox))

define Download/medvemodem-ubus
  PROTO:=git
  URL:=https://github.com/openwrt/ubus.git
  SOURCE_VERSION:=$(MEDVEMODEM_UBUS)
  SUBDIR:=ubus-$(MEDVEMODEM_UBUS)
  FILE:=medvemodem-ubus-$(MEDVEMODEM_UBUS).tar.zst
  MIRROR_HASH:=skip
endef
$(eval $(call Download,medvemodem-ubus))

define Download/medvemodem-uci
  PROTO:=git
  URL:=https://github.com/openwrt/uci.git
  SOURCE_VERSION:=$(MEDVEMODEM_UCI)
  SUBDIR:=uci-$(MEDVEMODEM_UCI)
  FILE:=medvemodem-uci-$(MEDVEMODEM_UCI).tar.zst
  MIRROR_HASH:=skip
endef
$(eval $(call Download,medvemodem-uci))

define Package/medvemodem
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=WWAN
  TITLE:=MedveModem modem orchestration engine
  DEPENDS:=+libjson-c
  # needed on the router, not to build: the SDK does not compile them (with their kernel modules) for every new
  # release line, apk and opkg still install them from the router's feed
  EXTRA_DEPENDS:=jsonfilter (>=0), modemmanager (>=0), qmi-utils (>=0)
endef

define Package/medvemodem/description
 USB modem manager for OpenWrt. For every modem it picks one owner -
 ModemManager, QModem or its own path (QMI, MBIM, AT dial through netifd) -
 keeps the others off the device, makes the netifd interface, watches the
 connection and recovers it step by step without restarting ModemManager.
 Modem profiles, priority and failover between WAN and modems, signal
 history, SMS and USSD, a support report without personal data.
 The medvemodemd daemon (ubus object "medvemodem"), the medvemodem CLI
 and its TUI.
endef

define Build/Prepare
	$(call Build/Prepare/Default)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
	zstd -dc $(DL_DIR)/medvemodem-libubox-$(MEDVEMODEM_LIBUBOX).tar.zst | $(TAR) -C $(PKG_BUILD_DIR) $(TAR_OPTIONS)
	zstd -dc $(DL_DIR)/medvemodem-ubus-$(MEDVEMODEM_UBUS).tar.zst | $(TAR) -C $(PKG_BUILD_DIR) $(TAR_OPTIONS)
	zstd -dc $(DL_DIR)/medvemodem-uci-$(MEDVEMODEM_UCI).tar.zst | $(TAR) -C $(PKG_BUILD_DIR) $(TAR_OPTIONS)
endef

MEDVEMODEM_STATIC := $(PKG_BUILD_DIR)/static
MEDVEMODEM_CFLAGS := -Wall -Wextra -Werror -std=gnu11 -DMEDVEMODEM_VERSION='"$(PKG_VERSION)-r$(PKG_RELEASE)"' \
	-I$(MEDVEMODEM_STATIC)/include
MEDVEMODEM_CORE := registry.c discovery.c classify.c identity.c sysfs.c profile.c evtext.c
MEDVEMODEM_LIBS := $(MEDVEMODEM_STATIC)/libubus.a $(MEDVEMODEM_STATIC)/libblobmsg_json.a $(MEDVEMODEM_STATIC)/libubox.a -ljson-c
MEDVEMODEM_CONFIG := cfgplan.c netcfg.c ucistore.c cfgtxn.c session.c at.c atport.c esim.c dial.c dial_at.c

define Build/Compile
	CC="$(TARGET_CC)" AR="$(TARGET_AR)" CFLAGS="$(TARGET_CFLAGS)" CPPFLAGS="$(TARGET_CPPFLAGS)" \
		JSONC_INC="$(STAGING_DIR)/usr/include/json-c" \
		./tools/static-libs.sh $(MEDVEMODEM_STATIC) $(PKG_BUILD_DIR)/libubox-$(MEDVEMODEM_LIBUBOX) \
		$(PKG_BUILD_DIR)/ubus-$(MEDVEMODEM_UBUS) $(PKG_BUILD_DIR)/uci-$(MEDVEMODEM_UCI)
	$(TARGET_CC) $(MEDVEMODEM_CFLAGS) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -DWITH_UBUS \
		-I$(PKG_BUILD_DIR) \
		-o $(PKG_BUILD_DIR)/medvemodem \
		$(addprefix $(PKG_BUILD_DIR)/,$(MEDVEMODEM_CORE)) $(PKG_BUILD_DIR)/cli.c $(PKG_BUILD_DIR)/top.c \
		$(PKG_BUILD_DIR)/diag.c $(PKG_BUILD_DIR)/roles.c $(PKG_BUILD_DIR)/dnscheck.c $(PKG_BUILD_DIR)/websig.c \
		$(TARGET_LDFLAGS) $(MEDVEMODEM_LIBS)
	$(TARGET_CC) $(MEDVEMODEM_CFLAGS) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) \
		-I$(PKG_BUILD_DIR) \
		-o $(PKG_BUILD_DIR)/medvemodemd \
		$(addprefix $(PKG_BUILD_DIR)/,$(MEDVEMODEM_CORE)) \
		$(PKG_BUILD_DIR)/uevent.c $(PKG_BUILD_DIR)/history.c $(PKG_BUILD_DIR)/recovery.c $(PKG_BUILD_DIR)/hostfix.c $(PKG_BUILD_DIR)/watch.c $(PKG_BUILD_DIR)/uplink.c $(PKG_BUILD_DIR)/apn.c $(PKG_BUILD_DIR)/mmgr.c \
		$(PKG_BUILD_DIR)/roles.c $(PKG_BUILD_DIR)/lease.c $(PKG_BUILD_DIR)/sms.c $(PKG_BUILD_DIR)/simid.c $(PKG_BUILD_DIR)/pdu.c $(PKG_BUILD_DIR)/sms_at.c $(PKG_BUILD_DIR)/presets.c $(PKG_BUILD_DIR)/radio.c $(PKG_BUILD_DIR)/failover.c $(PKG_BUILD_DIR)/quota.c $(PKG_BUILD_DIR)/sighist.c \
		$(PKG_BUILD_DIR)/mgr_radio.c $(PKG_BUILD_DIR)/mgr_quota.c $(PKG_BUILD_DIR)/mgr_sms_seen.c $(PKG_BUILD_DIR)/mgr_signal.c \
		$(PKG_BUILD_DIR)/mgr_sms.c $(PKG_BUILD_DIR)/mgr_sim.c $(PKG_BUILD_DIR)/mgr_web.c $(PKG_BUILD_DIR)/mgr_usbconf.c $(PKG_BUILD_DIR)/mgr_usbtree.c $(PKG_BUILD_DIR)/mgr_modemclass.c $(PKG_BUILD_DIR)/mgr_usbrescue.c $(PKG_BUILD_DIR)/usbtree.c $(PKG_BUILD_DIR)/mgr_esim.c $(PKG_BUILD_DIR)/mgr_at_history.c $(PKG_BUILD_DIR)/mgr_foreign.c $(PKG_BUILD_DIR)/mgr_downtime.c $(PKG_BUILD_DIR)/downtime.c $(PKG_BUILD_DIR)/mgr_traffic.c $(PKG_BUILD_DIR)/trafhist.c $(PKG_BUILD_DIR)/mgr_mmlog.c $(PKG_BUILD_DIR)/mmlog.c $(PKG_BUILD_DIR)/mgr_neighbours.c $(PKG_BUILD_DIR)/mgr_alert.c $(PKG_BUILD_DIR)/mgr_wlmode.c $(PKG_BUILD_DIR)/mgr_redirect.c $(PKG_BUILD_DIR)/cellev.c $(PKG_BUILD_DIR)/alert.c $(PKG_BUILD_DIR)/mgr_clash.c $(PKG_BUILD_DIR)/mgr_procs.c $(PKG_BUILD_DIR)/mgr_kfault.c $(PKG_BUILD_DIR)/kfault.c $(PKG_BUILD_DIR)/subnet.c $(PKG_BUILD_DIR)/chain.c \
		$(addprefix $(PKG_BUILD_DIR)/,$(MEDVEMODEM_CONFIG)) \
		$(PKG_BUILD_DIR)/medvemodemd.c \
		$(TARGET_LDFLAGS) $(MEDVEMODEM_STATIC)/libuci.a $(MEDVEMODEM_LIBS)
	$(TARGET_CC) $(MEDVEMODEM_CFLAGS) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) \
		-o $(PKG_BUILD_DIR)/medvemodem-webproxy $(PKG_BUILD_DIR)/webproxy.c $(TARGET_LDFLAGS)
	$(TARGET_CC) $(MEDVEMODEM_CFLAGS) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) \
		-I$(PKG_BUILD_DIR) \
		-o $(PKG_BUILD_DIR)/esim-apdu \
		$(PKG_BUILD_DIR)/esim_apdu.c $(PKG_BUILD_DIR)/apdu.c $(PKG_BUILD_DIR)/atport.c $(PKG_BUILD_DIR)/at.c \
		$(TARGET_LDFLAGS) -ljson-c
endef

define Package/medvemodem/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/medvemodem $(1)/usr/sbin/medvemodem
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/medvemodemd $(1)/usr/sbin/medvemodemd
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/medvemodem-webproxy $(1)/usr/sbin/medvemodem-webproxy
	$(INSTALL_BIN) ./tools/bench/medvemodem-bench.sh $(1)/usr/sbin/medvemodem-bench
	$(INSTALL_BIN) ./tools/bench/medvemodem-stand.sh $(1)/usr/sbin/medvemodem-stand

	$(INSTALL_DIR) $(1)/usr/share/medvemodem/profiles/generic
	$(INSTALL_DIR) $(1)/usr/share/medvemodem/profiles/catalog
	$(INSTALL_DATA) ./profiles/schema.json $(1)/usr/share/medvemodem/profiles/schema.json
	$(INSTALL_DATA) ./files/usr/share/medvemodem/netcheck.list $(1)/usr/share/medvemodem/netcheck.list
	$(INSTALL_DATA) ./files/usr/share/medvemodem/dnscheck.list $(1)/usr/share/medvemodem/dnscheck.list
	$(INSTALL_DIR) $(1)/usr/share/medvemodem/presets
	$(INSTALL_DATA) ./profiles/presets/*.json $(1)/usr/share/medvemodem/presets/
	$(INSTALL_DIR) $(1)/usr/share/medvemodem/qmodem
	printf 'version=%s\ncommit=%s\narches=%s\nbase=%s\n' '$(QMODEM_VERSION)' '$(QMODEM_COMMIT)' '$(QMODEM_ARCHES)' '$(MEDVEMODEM_DOWNLOAD_BASE)' \
		> $(1)/usr/share/medvemodem/qmodem/source
	$(INSTALL_DIR) $(1)/usr/share/medvemodem/apn/licenses
	$(INSTALL_DATA) ./profiles/apn/providers.tsv ./profiles/apn/overrides.list ./profiles/apn/NOTICE $(1)/usr/share/medvemodem/apn/
	$(INSTALL_DATA) ./profiles/apn/licenses/* $(1)/usr/share/medvemodem/apn/licenses/
	$(INSTALL_DATA) ./profiles/generic/*.json $(1)/usr/share/medvemodem/profiles/generic/
	$(INSTALL_DATA) ./profiles/catalog/*.json $(1)/usr/share/medvemodem/profiles/catalog/

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/medvemodem $(1)/etc/config/medvemodem

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/medvemodemd $(1)/etc/init.d/medvemodemd

	$(INSTALL_DIR) $(1)/usr/libexec/medvemodem
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/config-reload $(1)/usr/libexec/medvemodem/config-reload
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/esim-lpac $(1)/usr/libexec/medvemodem/esim-lpac
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/apn-update $(1)/usr/libexec/medvemodem/apn-update
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/bg $(1)/usr/libexec/medvemodem/bg
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/qmodem-install $(1)/usr/libexec/medvemodem/qmodem-install
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/pkg-install $(1)/usr/libexec/medvemodem/pkg-install
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/route-metric $(1)/usr/libexec/medvemodem/route-metric
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/neighbours $(1)/usr/libexec/medvemodem/neighbours
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/ttl-fix $(1)/usr/libexec/medvemodem/ttl-fix
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/netcheck $(1)/usr/libexec/medvemodem/netcheck
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/web-forward $(1)/usr/libexec/medvemodem/web-forward
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/sms-forward $(1)/usr/libexec/medvemodem/sms-forward
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/sms-alert $(1)/usr/libexec/medvemodem/sms-alert
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/dns-follow $(1)/usr/libexec/medvemodem/dns-follow
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/conntrack-flush $(1)/usr/libexec/medvemodem/conntrack-flush
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/svc-restart $(1)/usr/libexec/medvemodem/svc-restart
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/tunnel-probe $(1)/usr/libexec/medvemodem/tunnel-probe
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/wlmode $(1)/usr/libexec/medvemodem/wlmode
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/speedtest $(1)/usr/libexec/medvemodem/speedtest
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/proxy-probe $(1)/usr/libexec/medvemodem/proxy-probe
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/local-proxy $(1)/usr/libexec/medvemodem/local-proxy
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/self-update $(1)/usr/libexec/medvemodem/self-update
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/fcc-unlock-foxconn $(1)/usr/libexec/medvemodem/fcc-unlock-foxconn
	$(INSTALL_BIN) ./files/usr/libexec/medvemodem/service $(1)/usr/libexec/medvemodem/service
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/esim-apdu $(1)/usr/libexec/medvemodem/esim-apdu

	$(INSTALL_DIR) $(1)/lib/netifd/proto
	$(INSTALL_BIN) ./files/lib/netifd/proto/medvemodem.sh $(1)/lib/netifd/proto/medvemodem.sh

	$(INSTALL_DIR) $(1)/etc/hotplug.d/tty
	$(INSTALL_BIN) ./files/etc/hotplug.d/tty/26-medvemodem-mm-ports $(1)/etc/hotplug.d/tty/26-medvemodem-mm-ports
	$(INSTALL_BIN) ./files/etc/hotplug.d/tty/26-medvemodem-mm-frozen $(1)/etc/hotplug.d/tty/26-medvemodem-mm-frozen
	$(INSTALL_DIR) $(1)/etc/hotplug.d/net $(1)/etc/hotplug.d/usbmisc
	$(INSTALL_BIN) ./files/etc/hotplug.d/tty/26-medvemodem-mm-frozen $(1)/etc/hotplug.d/net/26-medvemodem-mm-frozen
	$(INSTALL_BIN) ./files/etc/hotplug.d/tty/26-medvemodem-mm-frozen $(1)/etc/hotplug.d/usbmisc/26-medvemodem-mm-frozen

	$(INSTALL_DIR) $(1)/lib/upgrade/keep.d
	$(INSTALL_DATA) ./files/lib/upgrade/keep.d/medvemodem $(1)/lib/upgrade/keep.d/medvemodem
endef

# procd keeps a running service when "start" sees the same command line, so an
# upgrade would leave the old daemon in memory: restart it on a live system,
# after an install and an upgrade alike (the owner, 2026-10-25); a service the
# operator switched off stays off.
define Package/medvemodem/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] && exit 0
/etc/init.d/medvemodemd enabled && /etc/init.d/medvemodemd restart
exit 0
endef

# The engine's ModemManager port rules (serial ports of mm_ports "control"
# modems) must not outlive it; after an upgrade the restarted engine writes
# them again. Neither do the FCC unlock links the operator turned on: a link
# to a hook that is gone would fail ModemManager's attempts (HANDOFF 176).
define Package/medvemodem/postrm
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] && exit 0
rm -f /lib/udev/rules.d/78-mm-medvemodem.rules
[ -x /usr/libexec/medvemodem/fcc-unlock-foxconn ] && exit 0
for l in /etc/ModemManager/fcc-unlock.d/*; do
	[ "$$(readlink "$$l" 2>/dev/null)" = /usr/libexec/medvemodem/fcc-unlock-foxconn ] && rm -f "$$l"
done
exit 0
endef

$(eval $(call BuildPackage,medvemodem))
