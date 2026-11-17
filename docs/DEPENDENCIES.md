# Зависимости пакетов MedveModem и QModem / Package dependencies

Источник истины: `Makefile` и `packages/*/Makefile` в этом репозитории, а для QModem — [FUjr/QModem, закреплённый коммит `3196ab787e06f93ef343d0beceadd9758e78ef8b`](https://github.com/FUjr/QModem/tree/3196ab787e06f93ef343d0beceadd9758e78ef8b/application). Состав ниже — **заявленные зависимости пакетов**, а не гарантия наличия каждого имени в каждом feed. `apk` (OpenWrt 25.12.6) и `opkg` (OpenWrt 24.10.8) сами разрешают `DEPENDS`/`EXTRA_DEPENDS`; ручная установка всего списка не нужна.

## Наш комплект: обязательные зависимости (r2)

| Пакет | Заявленные зависимости из Makefile | Установка |
| --- | --- | --- |
| `medvemodem` | `libjson-c`; `jsonfilter (>=0)`, `modemmanager (>=0)`, `qmi-utils (>=0)` как `EXTRA_DEPENDS` | **Движок обязателен**. Даже если отдельному модему не нужен ModemManager, текущий пакетный рецепт формально требует его. `libubox`, `libubus`, `libuci` статически собраны в движок, отдельно для него не нужны. |
| `luci-app-medvemodem` | `luci-base`, `medvemodem` и **точная версия** `medvemodem (=0.16.0_alpha1-r2)` через `EXTRA_DEPENDS` | Для LuCI обязателен; движок и интерфейс должны быть **одной ревизии**. `luci-base/host` — зависимость **сборки**, не роутера. |
| `medvemodem-bot` | `medvemodem (>=0.16.0_alpha1-r2)`, `ucode`, `ucode-mod-ubus`, `ucode-mod-uloop`, `ucode-mod-fs`, `ucode-mod-uci`, `ucode-mod-log`, `ucode-mod-digest`, `curl`, `ca-bundle`, `jq` | Опционально, только для Telegram-бота. |
| `luci-app-medvemodem-bot` | `luci-base`, `medvemodem-bot`, `luci-app-medvemodem` | Опционально, вместе с ботом, если нужна его страница LuCI. |
| `medvemodem-lpac` | `libcurl` | Опционально для расширенной работы с eSIM. Его `stdio`-драйвер входит в базовый пакет. Независимая версия lpac **не обязана** иметь ревизию движка r2. |
| `medvemodem-lpac-uqmi` | `medvemodem-lpac`, `uqmi (>=0)` через `EXTRA_DEPENDS` | Только при нужном QMI/APDU-транспорте. |
| `medvemodem-lpac-mbim` | `medvemodem-lpac`, `libmbim` | Только при нужном MBIM/APDU-транспорте; этот пакет не обязательно входит в стандартную сборочную матрицу. |
| `medvemodem-lpac-qmi` | `medvemodem-lpac`, `libqmi` | Только при нужном libqmi/APDU-транспорте; этот пакет не обязательно входит в стандартную сборочную матрицу. |

**Важно:** формальная зависимость `modemmanager` в `medvemodem` отличается от выбора владельца модема. Можно вести конкретный модем собственным движком или QModem, но пакетный менеджер всё равно потребует объявленные зависимости.

## Отдельный QModem Release: зависимости закреплённого upstream

[Постоянный QModem Release](https://github.com/Medvedolog/medvemodem/releases/tag/qmodem-3.3.0_p20261108-r1): шесть архивов под `aarch64_cortex-a53`, `aarch64_generic`, `x86_64` и линии OpenWrt `24.10`/`25.12`. Установщик MedveModem получает его отдельно; QModem LuCI **не входит** в этот выпуск.

| Пакет | Зависимости в закреплённом исходном Makefile |
| --- | --- |
| `qmodem` | `kmod-usb2`, `kmod-usb3`, `kmod-usb-serial`, `kmod-usb-serial-option`, `kmod-usb-serial-qualcomm`, `kmod-usb-net`, `kmod-usb-acm`, `kmod-usb-wdm`, `kmod-usb-net-cdc-ether`, `kmod-usb-net-cdc-mbim`, `kmod-usb-net-rndis`, `kmod-usb-net-cdc-ncm`, `kmod-usb-net-huawei-cdc-ncm`, `ubus-at-daemon`, `tom_modem`, `terminfo`, `sms-tool_q`, `modem_scan`, `jq`, `flock`, `bc`, `coreutils`, `coreutils-stat`, `xxd`, `usbutils`; плюс условные зависимости ниже. |
| `qmodem-seal` | `libsodium`; включается условным параметром `PACKAGE_qmodem_INCLUDE_SEAL` (в upstream по умолчанию включён). |
| `tom_modem` | `libubus`, `libubox`, `libjson-c`, `libblobmsg-json` |
| `ubus-at-daemon` | `libubus`, `libubox`, `libblobmsg-json`, `libjson-c` |
| `sms-tool_q` | В Makefile дополнительные `DEPENDS` не объявлены. |
| `modem_scan` | `libjson-c`, `tom_modem`, `sms-tool_q`, `ubus-at-daemon` |
| `quectel-CM-5G-M` | `libjson-c` |
| `ndisc6` | Дополнительных `DEPENDS` в конкретном определении `Package/ndisc6` нет; иные подпакеты этого исходника имеют собственные зависимости. В нашем сборщике этот пакет **необязателен** и копируется только если получен. |

Условные зависимости в upstream `qmodem` (зависят от конфигурации сборки): `rdisc6`/`ndisc6`; `kmod-usb-net-qmi-wwan`; vendor QMI `kmod-qmi_wwan_q`, `kmod-qmi_wwan_f`, `kmod-qmi_wwan_s`; NSS `kmod-rmnet-nss`, `kmod-qmi_wwan_q_nss`, `kmod-pcie_mhi_nss`; MHI/PCIe `kmod-pcie_mhi`, `kmod-mhi-net`, `kmod-mhi-pci-generic`, `kmod-mhi-wwan-ctrl`, `kmod-mhi-wwan-mbim`; `pciutils`; MTK T7xx `umbim`, `kmod-mtk-t7xx`; `qfirehose`; и альтернативные Quectel CM `quectel-CM-5G-M`, `quectel-CM-5G`, `quectel-cm`. **Это не список для безусловной установки**: доступность и выбор зависят от устройства, SDK и конфигурационных флагов.

**Важная особенность:** pinned QModem объявляет **оба** `kmod-usb2` и `kmod-usb3` без условий. Для роутера без USB 3.0 это не означает наличие порта USB 3.0, но пакетный менеджер всё равно проверяет существование и совместимость `kmod-usb3` в его feeds. Нельзя обходить несовпадение драйверов `--force-depends`/`--force-overwrite`. Модули `kmod-*` должны соответствовать **точному ядру** установленной прошивки, а не просто линии OpenWrt.

## Команды проверки перед установкой / Dependency checks

```sh
. /etc/openwrt_release
printf 'OpenWrt=%s\nArch=%s\n' "$DISTRIB_RELEASE" "$DISTRIB_ARCH"
command -v apk || command -v opkg
# OpenWrt 25.12.6:
apk update
apk info -a libjson-c luci-base modemmanager qmi-utils jsonfilter
# OpenWrt 24.10.8 (альтернатива, НЕ выполнять оба блока):
opkg update
opkg info libjson-c luci-base modemmanager qmi-utils jsonfilter
```

Перед **установкой QModem** LuCI-установщик делает пробный расчёт зависимостей (`opkg install --noaction` или `apk add --simulate --allow-untrusted`) и не продолжает установку при недоступных зависимостях. Отсутствие пакета в feed означает ограничение именно этой прошивки/архитектуры; не рекомендуется брать чужие `kmod-*` из других сборок OpenWrt.

Для базовой установки MedveModem обычно достаточно выбрать **два своих пакета** из одного архива: `medvemodem` и `luci-app-medvemodem`. Все их объявленные зависимости подтянет менеджер пакетов из подключённых feeds. Не устанавливайте всё содержимое шестиплатформенного релиза, не смешивайте `.apk` и `.ipk`, и не путайте `SHA256SUMS` разных выпусков.

---

**English summary:** The table above is derived from the actual package Makefiles, not a blanket list of packages to install. `apk` on OpenWrt 25.12.6 and `opkg` on OpenWrt 24.10.8 resolve declared dependencies. Mandatory base pair: `medvemodem` + `luci-app-medvemodem`, with matching engine/LuCI version. Bot, bot LuCI, lpac and its APDU transport plugins, and the independent QModem core are optional. ModemManager, `qmi-utils`, `jsonfilter`, and `libjson-c` are currently declared by the MedveModem package; QModem declares many kernel USB drivers (including `kmod-usb3`) and its own helpers. All `kmod-*` packages must match the router's exact running kernel. See [English installation guide](INSTALL.en.md).
