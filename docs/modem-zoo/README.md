# 🐻 Modem Zoo

**Modem Zoo** — человекочитаемый слой над профилями, evidence и историческими скриптами MedveModem.

Задача Zoo — отвечать не только **«какие VID:PID/команды известны»**, но и **«откуда это известно, зачем существовал конкретный upstream-script, что он меняет и насколько этому наблюдению можно доверять»**.

## Источники

- **a field report / another modem manager** — основной исторический USB/PCIe inventory, topology/port probing, FCC/composition hazards и опасные AT-команды.
- **other scripts / a router maker's firmware another modem manager + modeminfo** — известные compositions, fixed-port observations, bring-up/APN/reinit, modeminfo и model-specific presets.
- **QModem** — современная model DB, port rules, vendor command layer, AT broker и SMS architecture.
- **other scripts** — многолетние field quirks, driver detection, SIM/SMS storage и vendor AT sequences.
- **Telegram field exports (2023–2026)** — live diagnostics, logs, recipes and contradictory field reports; curated without raw chat mirroring.
- **4PDA offline HTML (2026-10-15)** — source-anchored firmware/host-specific failures, USB mode, aggregation and recovery observations; no auto-execution.
- **another package 5G-Modem-Support + another package (2026-10-16)** — chipset-scoped GTUSBMODE read maps, FM350/FM150/RM5xx issues, an unmerged durable-SMS PR and a controller-crash report; checked against pinned files, not bench-validated.
- **another modem manager** — наблюдения по USB-классификации модемов, SMS, FM350, ZTE MF286D и UFI003; технические сведения учитываются без выделения версионных delta-релизов.
- **Vendor documentation (Quectel, Fibocom)** — product-generation boundaries, radio capability and physical interfaces; never proof of an OpenWrt composition.
- **MedveModem** — нормализация provenance, confidence, fixtures и будущая hardware verification.

Сейчас исходные данные уже содержат **226 hardware IDs**, **45 historical composition observations** и **131 QModem model records**.

## Навигация

- Источники и уровни доверия
- Scripts & recipes — что делают и зачем
- [AT command corpus — включая destructive/firmware-specific](AT-COMMANDS.md)
- [Extended QModem/other scripts AT reference](AT-REFERENCE.md)
- [Telegram field notes — 34,739 messages → 1,090 technical index entries → curated observations](TELEGRAM-FIELD-NOTES.md)
- Telegram package artifacts — exact IPK hashes and behavior
- [4PDA primary-source field notes — 119 observations / 44 offline regression specifications](4PDA-FIELD-NOTES.md)
- another package / another package field notes — external project issues and parser maps (2026-10-16)
- [Сводный каталог](CATALOG.md)
- [USB-композиции, mode switching, fastboot и EDL](COMPOSITION-AND-BOOT-MODES.md)

### Curated modem families

- [Fibocom FM350 / RW350 family](modems/fibocom-fm350.md)
- [Foxconn T99W175 family](modems/foxconn-t99w175.md)
- [Quectel RM520N / RM5xx Qualcomm 5G family](modems/quectel-rm520n.md)
- [Foxconn T77W968 / Dell DW5821e family](modems/foxconn-t77w968.md)
- [Quectel EC25 / EG25 / EP06 / EM12 LTE family](modems/quectel-lte-ec25-ep06-em12.md)
- [Fibocom L850 / L860 Intel XMM family](modems/fibocom-l850-l860.md)
- [Fibocom FM150 / FM160 Qualcomm 5G family](modems/fibocom-fm150-fm160.md)
- [Quectel RM551E-GL — USB/PCIe transport, live band apply, AT-polling contention](modems/quectel-rm551e.md)
- [Quectel RM530N-GL — Release 16 Sub-6+mmWave, USB/PCIe evidence](modems/quectel-rm530n.md)
- [Fibocom FM190W-GL — SDX75 generation, USB/PCIe transport evidence](modems/fibocom-fm190w.md)
- [Fibocom FM650-CN — UNISOC V510/V516 revision split](modems/fibocom-fm650.md)
- [Compal RXM-G1 / SG500M2-X / VOS5G](modems/compal-rxm-g1.md)
- [Sierra EM9190 / EM9191 / EM9291](modems/sierra-em919x.md)
- [SIMCom SIM7100 / SIM7600 / SIM79xx](modems/simcom-sim7x.md)
- [ZTE MF79 / MF286 / Tenda MF6 / OLAX](modems/zte-mf-family.md)
- [Huawei E3372 / HiLink](modems/huawei-hilink.md)
- [MikroTik R11e-LTE](modems/mikrotik-r11e.md)
- [Telit LM960 / LN960 / FN990](modems/telit-lm-fn.md)
- [iPhone USB tethering](modems/iphone-tether.md)

## Как устроено

~~~text
profiles/catalog/            a field report + other scripts hardware data
reference/evidence/          QModem + other scripts + Telegram + 4PDA + vendor evidence
docs/catalog-provenance.json cross-source provenance
reference/modem-zoo/         только editorial grouping и пояснения
            ↓
tools/build_modem_zoo.py
            ↓
docs/modem-zoo/
~~~

Markdown — **представление**, а не отдельная база истины. Если меняется факт, исправляется evidence/profile/provenance, после чего Zoo регенерируется (`python3 tools/build_modem_zoo.py`; `--check` (и `tests/test_modem_zoo.py`) ловит ручные правки generated pages).

## Confidence

- **verified** — проверено нами на конкретном hardware + firmware;
- **multi-source** — один факт независимо подтверждают несколько источников;
- **reference** — внешний источник, пока без собственной проверки;
- **hazard** — известный destructive side effect, конфликт ID или риск потери управления;
- **experimental** — команда/форма известна не полностью или подтверждена только косвенно;
- **contradictory** — источники расходятся; сохраняем оба наблюдения.
