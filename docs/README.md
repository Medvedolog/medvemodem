# MedveModem Documentation / Документация

[Repository / Репозиторий](../README.md) · [Modem Zoo / Зоопарк](modem-zoo/README.md)

Документация разделена по назначению. **Технические идентификаторы, API, примеры команд, имена файлов и JSON-поля не переводятся.** Полевые наблюдения и материалы зоопарка сохраняются на исходном языке.

| Раздел / Topic | Русский | English |
|---|---|---|
| Установка / Installation | [INSTALL.md](INSTALL.md) | [INSTALL.en.md](INSTALL.en.md) |
| Сборка / Building | [BUILDING.md](BUILDING.md) | [BUILDING.en.md](BUILDING.en.md) |
| Краткое руководство / User guide | [USER-GUIDE.md](USER-GUIDE.md) | [USER-GUIDE.en.md](USER-GUIDE.en.md) |
| Полное руководство / Manual | [MANUAL.md](MANUAL.md) | [MANUAL.en.md](MANUAL.en.md) |
| ubus API | [Русский путеводитель](UBUS-API.ru.md) | [Full API reference](UBUS-API.md) |
| Архитектура плагинов / Plugin architecture | [PLUGIN-ARCHITECTURE.ru.md](PLUGIN-ARCHITECTURE.ru.md) | [PLUGIN-ARCHITECTURE.md](PLUGIN-ARCHITECTURE.md) |
| Темы LuCI / LuCI themes | [LUCI-THEMES.ru.md](LUCI-THEMES.ru.md) | [LUCI-THEMES.md](LUCI-THEMES.md) |
| История / Changelog | [CHANGELOG.md](CHANGELOG.md) | [CHANGELOG.en.md](CHANGELOG.en.md) (r197–r195; partial) |
| Telegram-бот / Telegram bot | [README](../packages/medvemodem-bot/README.md) | [README.en.md](../packages/medvemodem-bot/README.en.md) |
| Modem Zoo / Зоопарк модемов | [modem-zoo/README.md](modem-zoo/README.md) | [Original field notes](modem-zoo/README.md) |

## Состояние перевода / Translation coverage

- **Full translated counterparts:** installation, building, quick user guide, full manual (11 chapters), Telegram bot guide, plugin architecture, LuCI theme guidelines.
- **Russian overview, English canonical reference:** ubus API. The Russian API overview is **not a full translation** of every method's arguments and field contracts.
- **Changelog:** English translation of public Alpha 1 and r197–r195; the complete 247 KB history remains in Russian.
- **Intentionally not translated:** field observations and modem zoo.
- **Telegram bot:** English translation is now available alongside the original Russian/mixed document.

Translations describe the corresponding source documents but **do not replace code-level verification**. The English ubus API reference is the canonical method/field catalog. For the current package build status, consult [Actions](https://github.com/Medvedolog/medvemodem/actions) and [Releases](https://github.com/Medvedolog/medvemodem/releases).

## For contributors / Для участников

Report discrepancies through [GitHub Issues](https://github.com/Medvedolog/medvemodem/issues). Include the exact firmware release, modem model, MedveModem version and a redacted diagnostic report. Do not publish IMEI, ICCID, IMSI, credentials or unredacted SMS messages.
