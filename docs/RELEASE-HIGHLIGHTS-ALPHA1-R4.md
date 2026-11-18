# MedveModem Alpha 1 r4 — hotfix

**Fix:** The r3 APK/IPK packages included LuCI code with the old version label `0.16.0-alpha1-r1`. After installation the dashboard displayed `MedveModem 0.16.0_alpha1-r3 · web interface 0.16.0-alpha1-r1 (another version)`, even though the updated web files were installed.

**Исправлено:**
- Приведены к версии `0.16.0-alpha1-r4` строки в `common.js`, `dashboard.js` и `settings.js`.
- Нормализовано сравнение версий движка и LuCI (`0.16.0_alpha1-r4` = `0.16.0-alpha1-r4`).
- В SDK-сборку добавлена проверка согласованности версий, которая предотвращает повторение.
- Проверка и установка Telegram-бота и его LuCI по отдельности через компоненты, включая установку с нуля.
- Исправлен вывод версии установленного пакета и текстов ошибок в обновляторе (`pr` вместо реальной версии).
- Переработана матрица белых списков: компактные названия модемов, светофоры проверки, устранение наложения текстов, отсутствие повторяющихся длинных диагностических абзацев.
- Изменения страницы «Белые списки» из r3 сохранены.

**Fix scope:** Version display and prebuild consistency checks only; no deliberate changes to modem orchestration.

**Packages:** OpenWrt 24.10.8 IPK and 25.12.6 APK; `aarch64_cortex-a53`, `aarch64_generic`, `x86_64`. Download the archive matching your target and use `SHA256SUMS` to verify it. QModem remains in its separate pinned permanent release.

**После обновления:** Если браузер сохранил старые LuCI-файлы, очистите кэш страницы (Ctrl+F5), затем повторно откройте интерфейс.

Full release history: [Русский](https://github.com/Medvedolog/medvemodem/blob/main/docs/CHANGELOG.md) · [English](https://github.com/Medvedolog/medvemodem/blob/main/docs/CHANGELOG.en.md).
