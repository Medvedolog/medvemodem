# MedveModem Documentation / Документация

**[Зависимости всех пакетов (APK / OPKG) / All package dependencies](DEPENDENCIES.md)** — MedveModem, LuCI, Telegram-бот, lpac, QModem, драйверы ядра и условные зависимости.



[Repository / Репозиторий](../README.md) · [Modem Zoo / Зоопарк](modem-zoo/README.md)

Документация разделена по назначению. **Технические идентификаторы, API, примеры команд, имена файлов и JSON-поля не переводятся.** Полевые наблюдения и материалы зоопарка сохраняются на исходном языке.

| Раздел / Topic | Русский | English |
|---|---|---|
| Установка / Installation | [INSTALL.md](INSTALL.md) | [INSTALL.en.md](INSTALL.en.md) |
| Сборка / Building | [BUILDING.md](BUILDING.md) | [BUILDING.en.md](BUILDING.en.md) |
| Краткое руководство / User guide | [USER-GUIDE.md](USER-GUIDE.md) | [USER-GUIDE.en.md](USER-GUIDE.en.md) |
| Полное руководство / Manual | [MANUAL.md](MANUAL.md) | [MANUAL.en.md](MANUAL.en.md) |
| ubus API | [Подробный русский справочник](UBUS-API.ru.md) | [Full API reference](UBUS-API.md) |
| Архитектура плагинов / Plugin architecture | [PLUGIN-ARCHITECTURE.ru.md](PLUGIN-ARCHITECTURE.ru.md) | [PLUGIN-ARCHITECTURE.md](PLUGIN-ARCHITECTURE.md) |
| Техническое ТЗ: SIM-слоты и eSIM / SIM-slot and eSIM handoff | [HANDOFF-SIM-SLOTS-ESIM.md](HANDOFF-SIM-SLOTS-ESIM.md) | [Технические идентификаторы сохранены](HANDOFF-SIM-SLOTS-ESIM.md) |
| Темы LuCI / LuCI themes | [LUCI-THEMES.ru.md](LUCI-THEMES.ru.md) | [LUCI-THEMES.md](LUCI-THEMES.md) |
| История / Changelog | [CHANGELOG.md](CHANGELOG.md) | [CHANGELOG.en.md](CHANGELOG.en.md) (all 238 entries) |
| Telegram-бот / Telegram bot | [README](../packages/medvemodem-bot/README.md) | [README.en.md](../packages/medvemodem-bot/README.en.md) |
| Modem Zoo / Зоопарк модемов | [modem-zoo/README.md](modem-zoo/README.md) | [Original field notes](modem-zoo/README.md) |

## eSIM — возможности Alpha 1 / Alpha 1 capabilities

**Русский:** MedveModem уже **обнаруживает eSIM** («Диагностика» → «Проверить eSIM»), **читает EID и установленные профили** и **переключает существующие профили** во вкладке «eSIM». **До Alpha 2** новые профили на встроенную eUICC или UICC/9eSIM загружайте через совместимый телефон с приложением карты либо EasyEUICC/OpenEUICC на Android. Загрузка и удаление из MedveModem пока недоступны. Подробнее — в [полном руководстве](MANUAL.md#531-esim).

**English:** Alpha 1 can **detect eSIM** (Diagnostics → Check for an eSIM), **read EID and installed profiles**, and **switch between already stored profiles** in the eSIM tab. **Until Alpha 2**, provision new profiles on an embedded eUICC or removable UICC/9eSIM through a compatible phone and card app or EasyEUICC/OpenEUICC on Android. In-app profile downloads and deletion are not yet available. See the [manual](MANUAL.en.md#531-esim).

## Состояние перевода / Translation coverage

- **Full translated counterparts:** installation, building, quick user guide, full manual (11 chapters), Telegram bot guide, plugin architecture, LuCI theme guidelines and complete release history (238 entries).
- **ubus API:** подробный русский справочник методов и полей с сохранением идентификаторов; английский оригинал остаётся исходным контрактом для сверки.
- **Changelog:** полный английский перевод всех 238 исторических записей от начала разработки до публичной Alpha 1; русский оригинал сохранён без изменений.
- **Intentionally not translated:** field observations and modem zoo.
- **Telegram bot:** English translation is now available alongside the original Russian/mixed document.

Translations describe the corresponding source documents but **do not replace code-level verification**. The English ubus API reference is the canonical method/field catalog. For the current package build status, consult [Actions](https://github.com/Medvedolog/medvemodem/actions) and [Releases](https://github.com/Medvedolog/medvemodem/releases).

## For contributors / Для участников

Report discrepancies through [GitHub Issues](https://github.com/Medvedolog/medvemodem/issues). Include the exact firmware release, modem model, MedveModem version and a redacted diagnostic report. Do not publish IMEI, ICCID, IMSI, credentials or unredacted SMS messages.
