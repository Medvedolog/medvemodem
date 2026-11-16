# SPDX-License-Identifier: GPL-3.0-only
#
# Build recipe for upstream lpac at a pinned git commit, for MedveModem's
# managed eSIM backend (docs/ESIM-INTEGRATION.md, docs/LPAC-BUILD.md). No lpac
# source is kept in this repository: both sources are fetched by commit.
#
# Why not the feed's lpac package: its 2.3.0 release has a broken stdio APDU
# driver (a sent request is taken for a failure; fixed upstream in 977c324,
# after the release), and MedveModem's AT transport needs that driver. Installed
# under /usr/lib/medvemodem/lpac so it never collides with the feed's lpac
# (which installs /usr/lib/lpac as a file).

include $(TOPDIR)/rules.mk

PKG_NAME:=medvemodem-lpac
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/estkme-group/lpac.git
PKG_SOURCE_DATE:=2026-09-19
PKG_SOURCE_VERSION:=82ada9e53251c7f0cb08bfd9060b17019a30242c
# The commit id pins the content; the generated tarball's hash depends on the
# OpenWrt release's packer, so it is not recorded.
PKG_MIRROR_HASH:=skip

# lpac builds against its own cJSON fork (upstream PRs 949/955 pending)
LPAC_CJSON_VERSION:=4818f043bda624b5738384eae3a0189b2bd1f5e1
LPAC_CJSON_SUBDIR:=cJSON-$(LPAC_CJSON_VERSION)
LPAC_CJSON_FILE:=$(LPAC_CJSON_SUBDIR).tar.zst
# what `lpac version` reports (upstream's git-describe form)
LPAC_VERSION_STRING:=2.3.0.r463.82ada9e53251

PKG_MAINTAINER:=Medvedolog
PKG_LICENSE:=AGPL-3.0-only LGPL-2.1-only MIT
PKG_LICENSE_FILES:=src/LICENSE driver/LICENSE euicc/LICENSE

PKG_CONFIG_DEPENDS:= \
	CONFIG_PACKAGE_medvemodem-lpac-uqmi \
	CONFIG_PACKAGE_medvemodem-lpac-mbim \
	CONFIG_PACKAGE_medvemodem-lpac-qmi

CMAKE_INSTALL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

LPAC_DIR:=/usr/lib/medvemodem/lpac

define Download/lpac-cjson
  PROTO:=git
  URL:=https://github.com/CoelacanthusHex/cJSON.git
  SOURCE_VERSION:=$(LPAC_CJSON_VERSION)
  SUBDIR:=$(LPAC_CJSON_SUBDIR)
  FILE:=$(LPAC_CJSON_FILE)
  MIRROR_HASH:=skip
endef
$(eval $(call Download,lpac-cjson))

define Package/medvemodem-lpac/Default
  SECTION:=utils
  CATEGORY:=Utilities
  URL:=https://github.com/estkme-group/lpac
endef

define Package/medvemodem-lpac
  $(call Package/medvemodem-lpac/Default)
  TITLE:=lpac eSIM LPA for MedveModem (pinned git build)
  DEPENDS:=+libcurl
endef

define Package/medvemodem-lpac/description
  lpac (SGP.22 LPA) built from a pinned upstream commit for MedveModem's
  managed eSIM backend: the stdio APDU driver used by the engine's AT bridge
  and the curl HTTP driver. Installed under /usr/lib/medvemodem/lpac; it does
  not replace or conflict with the feed's lpac package.
endef

define Package/medvemodem-lpac-uqmi
  $(call Package/medvemodem-lpac/Default)
  TITLE:=lpac uqmi APDU driver for MedveModem
  DEPENDS:=+medvemodem-lpac
  # the driver runs the uqmi program: needed on the router only (MedveModem's Makefile, EXTRA_DEPENDS)
  EXTRA_DEPENDS:=uqmi (>=0)
endef

define Package/medvemodem-lpac-mbim
  $(call Package/medvemodem-lpac/Default)
  TITLE:=lpac MBIM APDU driver for MedveModem (libmbim)
  DEPENDS:=+medvemodem-lpac +libmbim
endef

define Package/medvemodem-lpac-qmi
  $(call Package/medvemodem-lpac/Default)
  TITLE:=lpac QMI APDU driver for MedveModem (libqmi)
  DEPENDS:=+medvemodem-lpac +libqmi
endef

TARGET_CFLAGS += $(FPIC)

CMAKE_OPTIONS += \
	-DBUILD_TESTING=OFF \
	-DUSE_SYSTEM_DEPS=OFF \
	-DFETCHCONTENT_FULLY_DISCONNECTED=ON \
	-DFETCHCONTENT_SOURCE_DIR_CJSON=$(PKG_BUILD_DIR)/$(LPAC_CJSON_SUBDIR) \
	-DLPAC_VERSION=$(LPAC_VERSION_STRING) \
	-DLPAC_WITH_APDU_PCSC=OFF \
	-DLPAC_WITH_APDU_AT=OFF \
	-DLPAC_WITH_APDU_GBINDER=OFF \
	-DLPAC_WITH_APDU_QMI_QRTR=OFF \
	-DLPAC_WITH_HTTP_CURL=ON \
	-DLPAC_WITH_APDU_UQMI=$(if $(CONFIG_PACKAGE_medvemodem-lpac-uqmi),ON,OFF) \
	-DLPAC_WITH_APDU_MBIM=$(if $(CONFIG_PACKAGE_medvemodem-lpac-mbim),ON,OFF) \
	-DLPAC_WITH_APDU_QMI=$(if $(CONFIG_PACKAGE_medvemodem-lpac-qmi),ON,OFF) \
	-DCMAKE_SKIP_RPATH=FALSE \
	-DCMAKE_INSTALL_RPATH=$(LPAC_DIR) \
	-DDRIVER_INSTALL_PATH=$(LPAC_DIR)/driver

# lpac finds its libraries and driver plugins through its RUNPATH
# ($(LPAC_DIR) and $(LPAC_DIR)/driver); OpenWrt's cmake.mk skips RPATH by
# default, the options above turn it back on (rstrip keeps /usr/lib/* paths).

define Build/Prepare
	$(call Build/Prepare/Default)
	zstd -dc $(DL_DIR)/$(LPAC_CJSON_FILE) | $(TAR) -C $(PKG_BUILD_DIR) $(TAR_OPTIONS)
endef

define Package/medvemodem-lpac/install
	$(INSTALL_DIR) $(1)$(LPAC_DIR)/driver
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/lpac $(1)$(LPAC_DIR)/lpac
	$(CP) $(PKG_INSTALL_DIR)/usr/lib/libeuicc*.so* $(PKG_INSTALL_DIR)/usr/lib/liblpac-utils.so $(1)$(LPAC_DIR)/
	$(INSTALL_DATA) $(PKG_INSTALL_DIR)$(LPAC_DIR)/driver/driver_apdu_stdio.so $(1)$(LPAC_DIR)/driver/
	$(INSTALL_DATA) $(PKG_INSTALL_DIR)$(LPAC_DIR)/driver/driver_http_curl.so $(1)$(LPAC_DIR)/driver/
endef

define Package/medvemodem-lpac-uqmi/install
	$(INSTALL_DIR) $(1)$(LPAC_DIR)/driver
	$(INSTALL_DATA) $(PKG_INSTALL_DIR)$(LPAC_DIR)/driver/driver_apdu_uqmi.so $(1)$(LPAC_DIR)/driver/
endef

define Package/medvemodem-lpac-mbim/install
	$(INSTALL_DIR) $(1)$(LPAC_DIR)/driver
	$(INSTALL_DATA) $(PKG_INSTALL_DIR)$(LPAC_DIR)/driver/driver_apdu_mbim.so $(1)$(LPAC_DIR)/driver/
endef

define Package/medvemodem-lpac-qmi/install
	$(INSTALL_DIR) $(1)$(LPAC_DIR)/driver
	$(INSTALL_DATA) $(PKG_INSTALL_DIR)$(LPAC_DIR)/driver/driver_apdu_qmi.so $(1)$(LPAC_DIR)/driver/
endef

$(eval $(call BuildPackage,medvemodem-lpac))
$(eval $(call BuildPackage,medvemodem-lpac-uqmi))
$(eval $(call BuildPackage,medvemodem-lpac-mbim))
$(eval $(call BuildPackage,medvemodem-lpac-qmi))
