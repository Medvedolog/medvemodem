# Modem Zoo — 4PDA primary-source field notes (2026-10-15)

> **External forum reports, never bench validation.** No AT setters, firmware operations, EDL, NVM, USB composition changes, or USB power cycling are enabled from this dataset.

Corpus: **119 curated observations** (101 from large HTML archives; 18 from earlier 501-post HTML pages) and **44 non-operational regression specifications**. The full 4PDA HTML archives are NOT vendored into this repository.

## Provenance rules

- `#message-N` and `archive_sequence` are **offline archive sequence numbers, not 4PDA post permalinks**.
- Links go to the *topic*, not the exact forum message. Archive member/position and text SHA256 allow verification against the user-supplied offline ZIP.
- `FIX` means an author *reported* an outcome, not an independent reproducible bench result.
- Firmware, OEM, carrier, host OS, USB topology and revision qualifiers must survive any future extraction.
- `corroborates-existing-general-rule` means the Zoo already stated the general rule; this adds primary-source provenance and concrete scope instead of duplicating the rule.

## Input archive fingerprints (not distributed here)

- `4pda-994474-html-no-images.zip` — SHA256 `d8e6f395bb1be9526baa2574a06cada86df8faa9b3dfce7b4a04d4cd2aa19feb`.
- `4pda-four-topics-no-images.zip` — SHA256 `f21c5ad0ca0cf61b309de9b46439e3488fad9308ec01621b97926fb26e361180`.
- `4pda-1106442-1066668-no-images.zip` — SHA256 `50b0a004a984f9fdcce0f5733c2ff5e15c43bb6bd42adfcdba1cf6772af9b25d`.

## Scoped field reports

### fibocom-fm350 (14)

#### `4PDA-1057776-1734` — FM350-GL

**Classification:** `field-reported-fix` · **risk:** `write` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1057776) · 13.05.24, 19:26 · offline `4pda-four-topics/1057776/part-004.html#message-1734` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `06cf417766e4104e11d534b6da18f3b58066f125d74355645b2414c6fd73f31a`

**Reported observation:** После обновления до 81600.0000.00.29.23.16 a router maker's firmware не обнаруживал модем; после AT+GTUSBMODE=41 определение и связь восстановились.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** В профиле хранить FW, до/после PID и транспорт. Не отправлять команду автоматически.

#### `4PDA-1057776-4745` — FM350-GL (HP rev.01)

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1057776) · 18.01.25, 17:23 · offline `4pda-four-topics/1057776/part-010.html#message-4745` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `971598015cc850b5ac9bf5e0a3446214e734ac860a10a15b57cb38e5d4448fd8`

**Reported observation:** OpenWrt 23.05.5 на Beeline Turbo+: после reboot при том же SuperSpeed RNDIS устройство видно, но AT/интернет не работают; после физического переподключения работают. В проблемном состоянии интерфейс USB If#4 без driver, после переподключения занят option. Отдельно отмечены нагрев до 75°C и исчезновение USB-модемов до снятия питания роутера.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Различать cold/warm boot, отслеживать driver bind каждого интерфейса, сохранять lsusb -t и события netifd. Нет доказательства, что причиной был только нагрев.

#### `4PDA-1057776-7043` — FM350-GL Dell 3YCF7

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1057776) · 21.04.25, 16:57 · offline `4pda-four-topics/1057776/part-015.html#message-7043` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `889753e267ffce7f1665ca01723d8b88ad4c825e645a8e1cf823aec8fa500ebe`

**Reported observation:** Huasifei WS1208v2/OpenWrt: после GTDIPCMODE=3,1,1,1,3,15, GTUSBMODE=41 наблюдал 0e8d:7127, семь ttyUSB и AT на ttyUSB3; сеть показывала IP и метрики, но трафик не шёл ни на other scripts, ни на OpenWrt 24.10.0 с двумя вариантами управляющих пакетов.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Состояния connected/IP и Internet reachable не эквивалентны; проверять маршрут, DNS, ICMP по IP, PDP data-path; это НЕ подтверждённый фикс.

#### `4PDA-1057776-9505` — FM350-GL

**Classification:** `field-symptom` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1057776) · 29.03.26, 19:32 · offline `4pda-four-topics/1057776/part-020.html#message-9505` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `87249d7b99b82c6f14d33c456c114aea222646ed9a8bb08b7be1cdd72a345581`

**Reported observation:** После GTUSBMODE=40 пользователь получил только META+DEBUG без доступного AT-порта.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Сценарий потери AT-port; запрет автоматического перехода в mode40.

#### `4PDA-1057776-9507` — FM350-GL

**Classification:** `field-reported-fix` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1057776) · 29.03.26, 21:40 · offline `4pda-four-topics/1057776/part-020.html#message-9507` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `da7fb1dd3bf350d2d32ebabb87cca9d34c1cf43d202663e544cf1deaf9e31320`

**Reported observation:** Тот же автор подтвердил, что при META+DEBUG и отсутствии AT-порта помогла перепрошивка.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Recovery относится к сервисным действиям с согласованием FW и бэкапом; никакого циклического AT-switchover.

#### `4PDA-1057776-10759` — FM350-GL

**Classification:** `field-symptom` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1057776) · 07.09.26, 00:04 · offline `4pda-four-topics/1057776/part-022.html#message-10759` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `a566f02a1076ce92c69a9a96643d6813983c3b760e195689f5dcf88e982c501f`

**Reported observation:** Независимое сообщение 2026 года: mode40 оставил META+DEBUG, обратные AT недоступны, восстановление не подтверждено.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Сохранить второе свидетельство, но не записывать recovery как проверенное.

#### `4PDA-1057776-2206` — FM350-GL

**Classification:** `field-observation` · **risk:** `read` · **integration:** `corroborates-existing-general-rule` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1057776) · 22.06.24, 11:52 · offline `4pda-four-topics/1057776/part-005.html#message-2206` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `16b90ebc759fa2a1da80e564bdd1014745c7e90ec568e834bc44a70762effdef`

**Reported observation:** На виртуальном OpenMPTCProuter 0.60 / Linux 6.1.71 зафиксирован USB 0e8d:7127, SuperSpeed xHCI и успешный запуск FM350.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Успех привязан к xHCI/драйверам и версии системы, не переносится на ARM-host автоматически.

#### `4PDA-1057776-8434` — FM350-GL

**Classification:** `partial-result` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1057776) · 23.10.25, 16:41 · offline `4pda-four-topics/1057776/part-017.html#message-8434` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `28edbb95dd14c02fbc4aaef3a24e672052b826770d5074981736169421b4f979`

**Reported observation:** На Ubuntu mmcli -m показывает plugin generic, primary ttyUSB1 и два (at) ttyUSB1/ttyUSB3 при RNDIS; пользователь вручную поднял IP-стек, но низкая скорость осталась.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Получать порты из перечисления самого менеджера, не полагаться на единственный ttyUSB3; сетевое восстановление не путать с улучшением CA.

#### `4PDA-1057776-8595` — FM350-GL

**Classification:** `partial-result` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1057776) · 21.11.25, 02:38 · offline `4pda-four-topics/1057776/part-018.html#message-8595` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `2d00b3af38977c4fdce6e8a10c549c369e2bddd1204f9e3005ce0627782a70cc`

**Reported observation:** Debian Trixie/Raspberry Pi 5: пользователь сообщил несовместимость с ModemManager 1.24.0, при возврате к 1.20.4 ttyUSB снова появились; IP пришлось настраивать вручную.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Проверять version-aware обнаружение, сохранять модемные порты до/после обновления MM, не обещать универсальный откат.

#### `4PDA-1057776-1640` — FM350-GL

**Classification:** `negative-result` · **risk:** `write` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1057776) · 08.05.24, 03:19 · offline `4pda-four-topics/1057776/part-004.html#message-1640` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `026b33c61e8863a5a9365571fff8f673ce5d49a5c0b806c1d51f9792071f22d2`

**Reported observation:** Сообщено, что AT+EGMC=<op>,"usb_mode",1,<mode> описан в руководстве для выбора RNDIS/MBIM, однако после успешного ответа режим не изменился.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Не считать OK на AT достаточным доказательством фактического mode switch; проверять USB-enumeration.

#### `4PDA-1057776-3956` — FM350-GL

**Classification:** `field-reported-fix` · **risk:** `write` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1057776) · 22.10.24, 16:47 · offline `4pda-four-topics/1057776/part-008.html#message-3956` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `ca573490986a668cdd1da3e1d331707809192cfc490f5a62142270ed235febea`

**Reported observation:** Пользователь сообщил, что добавление завершающего параметра 1 в AT+GTACT позволило получить ожидаемый режим для B7; подтверждён только его результат.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Переключения band/ACT не делать без интерактивной диагностики и сохранения старого состояния.

#### `4PDA-1057776-5024` — FM350-GL

**Classification:** `field-symptom` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1057776) · 05.02.25, 12:29 · offline `4pda-four-topics/1057776/part-011.html#message-5024` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `26395a43b90c9ddae86b65bea5b018bac5330f440ab2217b52fab19ba06ba558`

**Reported observation:** На версии 29.23.21 автор сообщил, что после GTUSBMODE=40 a router maker's firmware показывает usbmodem и опасается невозможности возвращения без прошивки.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Маркировать наблюдение как неполную диагностику, не представлять предположение о recovery как факт.

#### `4PDA-1102595-1756` — WH3000 Pro/FM350-GL

**Classification:** `field-reported-fix` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1102595) · 11.07.26, 21:24 · offline `4pda-four-topics/1102595/part-004.html#message-1756` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `a4837f864757fb3887d73f5e1b44f3a0b3bdfa589082fd00a2170b369c296685`

**Reported observation:** Сборка OpenWrt 25.12.4 от a43 на WH3000 Pro eMMC: владелец FM350-GL сообщил нормальный запуск после выбора /dev/ttyUSB3.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Фиксировать ttyUSB3 как наблюдённый на конкретном устройстве, а не как глобальный AT-порт.

#### `4PDA-PRIMARY-MM-P05` — MM-P05 — Huasifei WH3000 / Pro

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1102595&st=240) · 19.07.2025 · offline `Huasifei_WH3000__Huasifei_WH3000_Pro__OpenWRT____обсуждение.html:printpost[255]` · source `Huasifei_WH3000__Huasifei_WH3000_Pro__OpenWRT____обсуждение.html` · whole-source-HTML SHA256 `9ec6798320632e9ff3707f49b32decf65d33ecf75e6baf91c686e1fca76c28d4`

**Reported observation:** WH3000 FM350 USB topology and serial-interface enumeration differs by host and package.

**Outcome limitation:** First 501 printposts only; see primary HTML and research extract for reported result.

**MedveModem implication (derived, not a statement by the author):** Record USB descriptors and attach generation before resolving AT ports.

### fibocom-l850-l860 (36)

#### `4PDA-994474-28112` — Fibocom L860-GL

**Classification:** `field-observation` · **risk:** `danger` · **integration:** `corroborates-existing-general-rule` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 11.10.22, 01:41 · offline `4pda-994474-html/part-057.html#message-28112` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `10bbd308f3a630fdf2d101b4e6e78e606fa1361706186fb4b85e0d3c397552dc`

**Reported observation:** Пользователь обновил L860 с 18601.5001.00.01.01.32 на .16.12_Secureboot, не наблюдал reboot; после обновления переключал NCM через записи cal_usbmode и store_nvm.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Команды модификации NVM нельзя использовать автоматически; режим NCM документировать только для указанных FW.

#### `4PDA-994474-24969` — Fibocom L860-GL

**Classification:** `negative-result` · **risk:** `read` · **integration:** `corroborates-existing-general-rule` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 25.07.22, 12:23 · offline `4pda-994474-html/part-050.html#message-24969` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `4b465ca9e80f236bf51d8a193e88eeb2c2b1b628319cd38c4af7e00939f5c0a2`

**Reported observation:** В L860-GL ответ ERROR на AT+GTUSBMODE=7 при попытке MBIM; переключение не подтверждено.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Не переносить синтаксис GTUSBMODE между FM350 и L860.

#### `4PDA-994474-39285` — Fibocom L860-GL

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 27.05.23, 22:34 · offline `4pda-994474-html/part-079.html#message-39285` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `d3aa7eecfe9042af36387ee50092a3c72c3513b4c1487cd09983bbe8a3067571`

**Reported observation:** В большом полевом отчёте L860 на Windows 11 сначала отображался Comneon Suspend (8087:095a); после установки USB WMC/FlashUsbDriver появились NCM/ACM порты. Зафиксированы обрывы под нагрузкой и проблемы регистрации/APN.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** До прошивки сравнить USB-драйверы, порт, питание/нагрев, связь AT/data. Нельзя считать все отказы «прошивочными».

#### `4PDA-994474-28248` — Fibocom L860-GL

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 13.10.22, 08:27 · offline `4pda-994474-html/part-057.html#message-28248` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `f8409a90d485a0e5d4030c5602f220be4b57245c9141e108adaac503574e9881`

**Reported observation:** Orange Pi Zero 2/Ubuntu 22.04: после AT+CGDATA=M-RAW_IP,1 порт входит в режим передачи данных, перестаёт принимать обычные AT; пользователь спрашивает о выходе из data mode.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Порт AT может быть занят data session, это не доказательство зависания и не основание для power-cycle.

#### `4PDA-994474-37897` — Fibocom L860-GL

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 29.04.23, 07:58 · offline `4pda-994474-html/part-076.html#message-37897` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `a85ca66a070f678a5e8652e98265778c6de4e6c6fb692133e1dff2c0b35a0809`

**Reported observation:** В MBIM после подключения к USB 3.0 пользователь наблюдал повторные отключения на ПК и ZBT; сам предполагал нехватку питания; причины не доказаны.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Проверить напряжение, адаптер, события xHCI и `lsusb -t`; не маркировать предположение о питании как установленный root cause.

#### `4PDA-994474-27352` — Fibocom L860-GL

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 26.09.22, 19:52 · offline `4pda-994474-html/part-055.html#message-27352` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `a49cddfc673f3caa024d29790c2304e7cd819ff67a90fa92a7281f312d72c473`

**Reported observation:** Пользователь после нескольких попыток прошивки сообщает CA B7+B7+B3, причём третья SCC появилась только под торрент-нагрузкой; измерения `at+xlec?`.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Отсутствие SCC в idle не доказывает неподдержку CA; использовать активный тест и отмечать нагрузку.

#### `4PDA-994474-7690` — L850/L860, EM7455, EP06, EM12, EM160R, T77W968, T99W175

**Classification:** `community-guide` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 20.03.21, 22:42 · offline `4pda-994474-html/part-016.html#message-7690` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `0cc23c9566ced6086348a8f23b06d607754b497a0b6d20c72caa5ea443d76cbc`

**Reported observation:** Крупный обновляемый полевой сравнительный пост: скорость, ток, USB2/USB3, питание и разные хосты. Автор предупреждает, что оборудование и методика менялись по разделам.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Хранить методику замеров и ревизии, не объединять цифры из разных тестовых стендов в одну сравнительную таблицу.

#### `4PDA-1066668-1` — Fibocom L850/L860-GL

**Classification:** `community-guide` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 27.03.23, 07:48 · offline `4pda-1106442-1066668/1066668/part-001.html#message-1` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `d888f64c327c6d84312733ffb114dad56b1b8568b870c9edb05f247242f99422`

**Reported observation:** Шапка различает L860-GL (ACM+NCM; USB MBIM способен уйти в reboot) и L850-GL (MBIM или ACM+NCM); обобщение "100%" не доказано для всех FW.

**Outcome limitation:** Общая предупреждающая инструкция, не аппаратный тест.

**MedveModem implication (derived, not a statement by the author):** Хранить capability per model+FW+bus, не распространять GTUSBMODE=7 между поколениями.

#### `4PDA-1066668-112` — Fibocom L850-GL

**Classification:** `community-guide` · **risk:** `danger` · **integration:** `corroborates-existing-general-rule` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 08.04.23, 23:03 · offline `4pda-1106442-1066668/1066668/part-001.html#message-112` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `eed4296379637e427accab9d3ab4cfdabd92f5b0d8f8aa872e5db27f0b33a503`

**Reported observation:** Приведена таблица NVM mode: 3 MBIM, 7 MBIM+GNSS+2ACM, 8 NCM+3ACM, 10 NCM+2ACM(2EP) и др.; отдельно AT+GTUSBMODE? / =?

**Outcome limitation:** Это копипаста автора для L850; соответствие значений актуальной FW не подтверждено.

**MedveModem implication (derived, not a statement by the author):** Сохранять как lookup/reference, не генерировать setter без проверки whitelist производителя.

#### `4PDA-1066668-207` — Fibocom L860-GL

**Classification:** `field-reported-fix` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 02.05.23, 10:54 · offline `4pda-1106442-1066668/1066668/part-001.html#message-207` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `6d19c67fee5bd9109f8684a72a08995b80e1f8b4770d6ccdb96fccfb253b3281`

**Reported observation:** После попытки MBIM и обновления FW появился циклический reboot; AT+GTUSBMODE в последующих прошивках не помогал.

**Outcome limitation:** Автор сообщал, что откатил на старую FW, через other scripts вернул NCM, затем обновил FW.

**MedveModem implication (derived, not a statement by the author):** Вводить manual recovery; FW downgrade не выполнять автоматически.

#### `4PDA-1066668-304` — Fibocom L860-GL

**Classification:** `community-guide` · **risk:** `write` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 18.05.23, 09:07 · offline `4pda-1106442-1066668/1066668/part-001.html#message-304` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `271a9b6afa20bee16a507c2b0a05200cc80c5807facaf31e3a123f6e2334a9a1`

**Reported observation:** Подключение OpenWrt с luci-proto-xmm/xmm-modem, proto=xmm, device=/dev/ttyACM0, APN, netifd нашёл CDC-NCM eth2 и назначил IP/DNS.

**Outcome limitation:** В сообщении приведён logread успешного `Interface wwan is now up`.

**MedveModem implication (derived, not a statement by the author):** Тестировать привязку AT и NCM по USB parent и явную настройку APN/адреса, не фиксировать ttyACM0.

#### `4PDA-1066668-570` — Fibocom L860-GL

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 21.06.23, 15:02 · offline `4pda-1106442-1066668/1066668/part-002.html#message-570` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `f0cc0dc080a33b26c119071ce94dcec946d2bd200ff2f1d0b8b5bba1818cc8e0`

**Reported observation:** В логе сначала 8087:07f9, затем USB disconnect и 8087:095a, `MODEM + 2 CDC-ACM + 3 CDC-NCM + SS`, ttyACM0..2, три CDC-NCM.

**Outcome limitation:** Автор противопоставил реальную USB перезагрузку обрыву сессии оператора.

**MedveModem implication (derived, not a statement by the author):** Тест временной шкалы и USB ID перехода, hotplug wait/rebind вместо immediate brick.

#### `4PDA-1066668-8873` — Fibocom L860-GL

**Classification:** `negative-result` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 03.04.24, 22:14 · offline `4pda-1106442-1066668/1066668/part-018.html#message-8873` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `4c94c4196f24205393f08f20b1618cfa6bc91e64a69da550d1aac96eb06e7fbf`

**Reported observation:** После неудачной перепрошивки L860 постоянно перезапускался; cal_usbmode=0 и 10 со store_nvm и CFUN=15 возвращали OK.

**Outcome limitation:** Bootloop при этом продолжился.

**MedveModem implication (derived, not a statement by the author):** OK от AT-setter ≠ успешная смена композиции / recovery.

#### `4PDA-1066668-10659` — Fibocom L860-GL

**Classification:** `field-reported-fix` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 05.08.24, 01:25 · offline `4pda-1106442-1066668/1066668/part-022.html#message-10659` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `2c0fb5d08bf8aaa78a41c8aadefdc6e83eb5a507eaeb965fee44710c26975172`

**Reported observation:** После затруднений с прошивкой автор указал cal_usbmode=10 либо 11, store_nvm, затем CFUN=15 после запуска прошивальщика.

**Outcome limitation:** Сообщено об успешном завершении flashing в одном случае.

**MedveModem implication (derived, not a statement by the author):** Строго сервисный режим с оператором и проверкой OEM/FW; запрещено в daemon.

#### `4PDA-1066668-12709` — Fibocom L850-GL

**Classification:** `community-guide` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 04.01.25, 23:07 · offline `4pda-1106442-1066668/1066668/part-026.html#message-12709` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `74d583e272fa7c5deb1f001439a500075800b264100ec735942e2f747a3fadae`

**Reported observation:** AT+GTUSBMODE=X автор описывает как самостоятельную команду; cal_usbmode.num=X требует at@store_nvm(cal_usbmode).

**Outcome limitation:** Успех второго пути на a router maker's firmware L850 не проверен самим автором.

**MedveModem implication (derived, not a statement by the author):** Автоматизация не должна смешивать two-stage NV write с AT+GTUSBMODE.

#### `4PDA-1066668-7195` — Fibocom L8x0

**Classification:** `community-guide` · **risk:** `danger` · **integration:** `corroborates-existing-general-rule` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 12.02.24, 15:57 · offline `4pda-1106442-1066668/1066668/part-015.html#message-7195` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `cb4a2b1c3737ecc5b5c7b7cb9f465bc4d6e7be11fe720e46f23ef29e066d9906`

**Reported observation:** Автор повторяет карту 0/1 reserved, 2 ECM+5ACM, 3–7 MBIM-наборы, 8–11 NCM-наборы, 12–14 serial-only.

**Outcome limitation:** Форумная таблица, без привязки ко всем FW.

**MedveModem implication (derived, not a statement by the author):** Каталог composition IDs хранит источник, назначение и confidence; serial-only modes опасны.

#### `4PDA-1066668-13285` — Fibocom L850-GL / Dell DW5820e

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 12.02.25, 09:54 · offline `4pda-1106442-1066668/1066668/part-027.html#message-13285` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `63f1da7afbcae4b290965e1708d0881e007ff2ec4946a70708efa018ca16f8b6`

**Reported observation:** Два L850 с firmware 18500.5001.00.05.27.30_5001.05.001.035 после `GTUSBMODE=7`: один 413c:81d9 DW5820e, другой 2cb7:0007 L850-GL.

**Outcome limitation:** На варианте 2cb7:0007 ModemManager на OpenWrt распознавался только после переподключения.

**MedveModem implication (derived, not a statement by the author):** Идентифицировать OEM / VID:PID; отдельный regression for post-reboot probe, без перепрошивки VID.

#### `4PDA-1066668-10532` — Два Fibocom L860-GL

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 21.07.24, 17:19 · offline `4pda-1106442-1066668/1066668/part-022.html#message-10532` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `c70ecc8ddc319b205a4aada9770170ecb919c0ad8f1eba6d2858459fbfb53001`

**Reported observation:** OpenWrt 23.05.3, два L860, 8087:095a + 2cb7:000b, USB2 hub; dmesg `Not enough host controller resources for new device state` и `can't set config #1, error -12`.

**Outcome limitation:** Автор менял VID:PID, USB2/USB3 хабы — проблема оставалась.

**MedveModem implication (derived, not a statement by the author):** Classify USB resource exhaustion; не лечить сменой IMEI/VID/PID и бесконечными resets.

#### `4PDA-1066668-10105` — Fibocom L850-GL

**Classification:** `partial-result` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 18.06.24, 17:28 · offline `4pda-1106442-1066668/1066668/part-021.html#message-10105` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `9f453c0b8ce86e7d3945291d4932caf93088550c430331b411ffee8900b2da79`

**Reported observation:** На OpenWrt NCM требовал ручного reconnect после reboot, MBIM устранил этот симптом, но автор наблюдал примерно 30 Мбит/с вместо ожидаемых ~100; на a router maker's firmware работало два года стабильно.

**Outcome limitation:** Наблюдение по нескольким хостам, без строгой контролируемой методики.

**MedveModem implication (derived, not a statement by the author):** Healthcheck cold_boot/reinit отдельно от throughput; не заявлять MBIM быстрее или стабильнее универсально.

#### `4PDA-1066668-17319` — Fibocom L850-GL

**Classification:** `field-reported-fix` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 07.09.26, 11:43 · offline `4pda-1106442-1066668/1066668/part-035.html#message-17319` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `a3ee2518a6c2eeaf729ef8cc8f1f7928e55c64ac2a0f66cde1f9cd4cc442057b`

**Reported observation:** Автор переключил L850 GTUSBMODE=7, в OpenWrt выбрал протокол ModemManager и APN, повторные reboot и hotplug работали.

**Outcome limitation:** После смены режима интернет стабилен у автора; на a router maker's firmware пропали метрики сигнала и потребовался ручной APN.

**MedveModem implication (derived, not a statement by the author):** Фикс ограничен конкретным стендом; устройство не конвертировать автоматически в MBIM.

#### `4PDA-1066668-15033` — Fibocom L850-GL / a router maker's firmware AX3000

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 02.09.25, 15:47 · offline `4pda-1106442-1066668/1066668/part-031.html#message-15033` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `8cee6db3ca074cae60d72b7d34f00a4d5114500e6e25d4e69b07aac25ed4dc7e`

**Reported observation:** На a router maker's firmware AX3000 `Setup xmm interface` нашёл ttyACM0 и eth2, PDP IPV4V6, затем `Failed to configure interface` и modem disconnected.

**Outcome limitation:** Подтверждённая ошибка netifd, исправление в этом посте отсутствует.

**MedveModem implication (derived, not a statement by the author):** Отдельный failed_configure state, собирать PDP/APN/proto и взаимодействие с control-port.

#### `4PDA-1066668-17226` — Fibocom L850-GL

**Classification:** `community-guide` · **risk:** `write` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 14.08.26, 05:46 · offline `4pda-1106442-1066668/1066668/part-035.html#message-17226` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `241554b1237428a3c030af9448efc29b2c202f24877f5232c40af5cfd9d86602`

**Reported observation:** Практик использовал luci-proto-xmm, luci-proto-mbim, mbim-utils и создал соответствующие интерфейсы; для его L850 ModemManager хуже переживал reboot.

**Outcome limitation:** Описан удачный метод на личном и удалённом стендах, точная версия FW не указана.

**MedveModem implication (derived, not a statement by the author):** Политика выбора владельца транспорта per device, не устанавливать конкурирующие менеджеры одновременно.

#### `4PDA-1066668-13206` — Fibocom L8x0

**Classification:** `community-guide` · **risk:** `write` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 06.02.25, 11:57 · offline `4pda-1106442-1066668/1066668/part-027.html#message-13206` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `1c890cc4fa6888705340f32b6cfc321c3928a2579904464b42ab62a84f4f6024`

**Reported observation:** При отсутствии подходящего feed автор перечислил /etc/gcom/xmm-*.gcom, /lib/netifd/proto/xmm.sh, hotplug helper и обязательный cdc-acm.ko.

**Outcome limitation:** Инструкция по ручному развёртыванию; прикреплённый xmm.zip не включён в архив.

**MedveModem implication (derived, not a statement by the author):** CI проверка наличия зависимостей и совместимости kernel modules; не скачивать shell pipe вслепую.

#### `4PDA-1066668-16498` — Fibocom L860-GL-16 / Orange Pi Zero 3

**Classification:** `field-observation` · **risk:** `write` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 15.04.26, 07:40 · offline `4pda-1106442-1066668/1066668/part-033.html#message-16498` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `abbf83561ba11e12d7e4870f625f635849283bedc9b09b4e120cf846ccf9671b`

**Reported observation:** Debian, FW 18601.5001.00.01.16.11, ttyACM0..2 и 3 NCM. mmcli выбрал PPP; dhclient не дал адрес; повторный CGACT=1,1 на активном context автор связал с крашем.

**Outcome limitation:** Автор поднял NCM через собственный foreground DTR holder, CGCONTRDP и CGDATA=M-RAW_IP; отключил MM/NM ownership.

**MedveModem implication (derived, not a statement by the author):** Перед CGACT проверять CGACT?; не применять DHCP к NCM по умолчанию; DTR lifetime; no competing MM/NM.

#### `4PDA-1066668-5226` — Fibocom L850-GL

**Classification:** `negative-result` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 22.12.23, 14:39 · offline `4pda-1106442-1066668/1066668/part-011.html#message-5226` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `e0f551799014bd4dc3ad1a045de88629a191ae4e9d09dd9c9cd3d356b0ec20ee`

**Reported observation:** OpenWrt 22.03.5, NCM proto-xmm работает с USB3; с USB2 шнуром либо отключённым kmod-usb3 lsusb не видит устройство; MBIM по USB2 виден и работает.

**Outcome limitation:** Модель и переходник не исключены как причина.

**MedveModem implication (derived, not a statement by the author):** Separate host bus/PHY issues from USB function settings; no blanket USB2 fallback.

#### `4PDA-1066668-15390` — Fibocom L850-GL / a router maker's firmware Viva KN-1912

**Classification:** `partial-result` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 11.10.25, 16:28 · offline `4pda-1106442-1066668/1066668/part-031.html#message-15390` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `3bffd734f1160afc580da92f5a57c9622ff6fa5c12b1a65e7c64aa0c0484454f`

**Reported observation:** Нагрузочный upload вызывал частые reboot каждые ~2–3 минуты. Кабели не помогли; отдельное питание улучшило стабильность, но a router maker's firmware утратил возможность power-cycle.

**Outcome limitation:** После a router maker's firmware 4.2.6→4.3.6 модем завис на «ожидание сети», fix в посте не найден.

**MedveModem implication (derived, not a statement by the author):** USB current & external-power detection; power-reset unavailable when externally powered; record OS regression.

#### `4PDA-1066668-8220` — Fibocom L860-GL / a router maker's firmware Giga

**Classification:** `field-reported-fix` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 11.03.24, 14:34 · offline `4pda-1106442-1066668/1066668/part-017.html#message-8220` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `6491312232ea2a0b114346d41664e019fed6d4d1cc9d8c83611b077707e3cc76`

**Reported observation:** На a router maker's firmware 4.0.7 после software restart USB-модем не обнаруживался; при 12V 5A дефект оставался.

**Outcome limitation:** Откат a router maker's firmware 4.0.7→4.0.5 восстановил обнаружение после 4 повторных reboot модема.

**MedveModem implication (derived, not a statement by the author):** Host firmware version в provenance и regression USB re-enumeration after soft-restart.

#### `4PDA-1066668-10960` — Fibocom L860-GL / a router maker's firmware Viva

**Classification:** `field-reported-fix` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 08.09.24, 18:30 · offline `4pda-1106442-1066668/1066668/part-022.html#message-10960` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `b0ef0d3c43b57205ef30964fac4918a97c2c9064a82d7a1794696f6b7713cf89`

**Reported observation:** До a router maker's firmware 4.2 beta3 L860 reboot 3–4 раза/день.

**Outcome limitation:** После upgrade в течение трёх дней отвалов не было (короткое окно).

**MedveModem implication (derived, not a statement by the author):** Host update can fix old firmware regressions; не обвинять modem FW автоматически.

#### `4PDA-1066668-10189` — Fibocom L860-GL

**Classification:** `field-reported-fix` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 24.06.24, 10:52 · offline `4pda-1106442-1066668/1066668/part-021.html#message-10189` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `25ae891a0c7d2dc2ac891aaef216cfc56b1d62f2ca36354c45e32372b6f1f136`

**Reported observation:** До обновления периодические проблемы обмена a router maker's firmware↔модем; Ping Check циклично пытался power-cycle без контроля внешнего питания.

**Outcome limitation:** После 18600.5001.00.35.00.27→18600.5001.00.35.01.57 автор наблюдал сутки нормальной работы.

**MedveModem implication (derived, not a statement by the author):** FW changes как сервисная операция и доказательство короткого срока, status not certified.

#### `4PDA-1066668-15134` — Fibocom L860-GL-16 / HiLink HLK-7621

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 18.09.25, 05:42 · offline `4pda-1106442-1066668/1066668/part-031.html#message-15134` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `f527fc9c703e44b5c2b1db190332962ee988f90af2261c1e520f58e2ef732cb9`

**Reported observation:** OpenWrt 24.10.2, FW 18601.5001.00.01.15.23_7000.03.001.011: AT-порт работает, IP есть, `eth1 link is down` без up, внешний ping отсутствует.

**Outcome limitation:** Помогал power-cycle; скрипт CFUN после ping loss предложен ИИ и в посте ещё не проверен.

**MedveModem implication (derived, not a statement by the author):** Distinct network_link_lost from AT alive and bearer alive; do NOT accept proposed CFUN reset as tested fix.

#### `4PDA-1066668-6030` — Fibocom L860-GL / a router maker's firmware

**Classification:** `field-reported-fix` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 20.01.24, 06:57 · offline `4pda-1106442-1066668/1066668/part-013.html#message-6030` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `82a3f9feeb15fd34c552f033fb1ca7dc1f4d77950839737aa65a3cfc4629ee7c`

**Reported observation:** Периодические перезагрузки модема коррелировали с механизмом Ping Check.

**Outcome limitation:** Автор наблюдал несколько дней без рестартов после отключения Ping Check.

**MedveModem implication (derived, not a statement by the author):** Автопроверки сети могут провоцировать циклы перезапуска; quarantine counters/per-device policy.

#### `4PDA-1066668-14447` — Fibocom L8x0

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 25.06.25, 17:07 · offline `4pda-1106442-1066668/1066668/part-029.html#message-14447` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `2d331b6b2f2951b478e3eb2e0efbd49d4ffbc95e0440ab34545044154e3343a7`

**Reported observation:** Автор вынул SIM при работе, модем потерял SIM и интернет; после возврата SIM она не появилась.

**Outcome limitation:** Работоспособность восстановилась после перезагрузки по питанию.

**MedveModem implication (derived, not a statement by the author):** Не обещать hot SIM recovery и не сбрасывать SIM программно бесконечно.

#### `4PDA-1066668-3557` — Fibocom L850/L860

**Classification:** `community-guide` · **risk:** `write` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 25.10.23, 07:46 · offline `4pda-1106442-1066668/1066668/part-008.html#message-3557` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `00309435423056b4bc825179eab2775b33c736146e2de0be61e999d0c0539c11`

**Reported observation:** a router maker's firmwareOS может повторно применять исключения бэндов при каждой инициализации; OpenWrt/other scripts/Windows описаны с другими правилами AT+XACT.

**Outcome limitation:** Автор не проверил все версии/хосты, но описывает реальные различия конфигурации.

**MedveModem implication (derived, not a statement by the author):** Band ownership и выбор диапазонов должны иметь источник: UCI/UI/actual modem; lock not equal active CA.

#### `4PDA-1066668-9590` — Fibocom L860-GL-16

**Classification:** `community-guide` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 12.05.24, 07:48 · offline `4pda-1106442-1066668/1066668/part-020.html#message-9590` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `2ffba5518efe0d71dd358fa5d91acc70ca0009b599100d9ef7fac3250afb33ee`

**Reported observation:** Для L860GL-16 дано различие NCM/MBIM eSIM-процедур, специфичные NVM slot settings и необходимость специализированного LPAC-MBIM wrapper для ETSI 102 221.

**Outcome limitation:** Автор рекомендует NCM как менее рискованный путь; бинарные приложения во вложениях отсутствуют.

**MedveModem implication (derived, not a statement by the author):** eSIM service plugin отдельный от netifd; блокировать опасные eUICC/NVM операции по умолчанию.

#### `4PDA-1066668-4196` — Fibocom L860-GL-16

**Classification:** `partial-result` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668) · 10.11.23, 14:12 · offline `4pda-1106442-1066668/1066668/part-009.html#message-4196` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `c04de952be56874efc18a46e32b4c5fafb36a81494d7f7b4bada724a5a5c6e2a`

**Reported observation:** После Lenovo 17.09 и cal_usbmode=7 пользователь видел MBIM адаптер после нескольких reboot, но интернет не работал без дополнительных команд в Windows.

**Outcome limitation:** На a router maker's firmware MBIM дал интернет, но без AT-порта и уровня сигнала.

**MedveModem implication (derived, not a statement by the author):** Probe enumerated/driver-ready/bearer-up separately; USB MBIM ≠ useful WAN.

#### `4PDA-PRIMARY-MM-P01` — MM-P01 — netis N6

**Classification:** `community-guide` · **risk:** `write` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1065564&st=200) · 28.11.2023 · offline `netis_N6___обсуждение.html:printpost[218]` · source `netis_N6___обсуждение.html` · whole-source-HTML SHA256 `d78418d5ff02e0c34d9c03cd52466c9882c4bc92ada05172cc4b2df312b31435`

**Reported observation:** Netis N6/OpenWrt 23.05-rc4: L850 NCM→MBIM; driver/umbim packages, APN and WAN configuration required; working link reported.

**Outcome limitation:** First 501 printposts only; see primary HTML and research extract for reported result.

**MedveModem implication (derived, not a statement by the author):** Do not enable GTUSBMODE=7 automatically, package install depends on Internet bootstrap.

### foxconn-t77w968 (5)

#### `4PDA-1102595-567` — DW5821e/T77W968

**Classification:** `field-reported-fix` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1102595) · 04.09.25, 17:00 · offline `4pda-four-topics/1102595/part-002.html#message-567` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `5bf807b1f27ae2b8ac17d5055892630aefad02a34ae4399a8d24e70a3cebca39`

**Reported observation:** В WH3000 Pro внутренний M.2 давал 413c:81d6 вместо ожидаемого 413c:81d7; автор сообщил результат после изоляции RESET pin67. ModemManager-интерфейс ломался после reboot, нативный MBIM /dev/cdc-wdm0 затем поднялся.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Отличать hardware pin-state и драйверную ошибку; pin insulation — только ручная аппаратная процедура, не универсальная рекомендация.

#### `4PDA-1102595-561` — WH3000 Pro/DW5821e

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1102595) · 03.09.25, 14:44 · offline `4pda-four-topics/1102595/part-002.html#message-561` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `a17dd08b277f840d6a6e02d9e07abc192d249a2dbc4e0f55206a0509c21d852c`

**Reported observation:** При внутреннем M.2-монтаже T77W968 не появлялся в настройках протокола, lsusb показывал 413c:81d6; тот же модем работал во внешнем USB-переходнике через ModemManager.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Стратегия диагностики должна учитывать physical USB path, wiring RESET и отличать внешний USB от внутреннего слота.

#### `4PDA-994474-5215` — DW5821e/T77W968

**Classification:** `community-guide` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 16.01.21, 21:00 · offline `4pda-994474-html/part-011.html#message-5215` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `7b9a8999776c2df679ad245022f98fbf4028fe0f9a45e54e5761da945cbcd33a`

**Reported observation:** Инструкция обсуждает VZ/OEM carrier profile и невозможность регистрации/CA при некоторых SIM/FW; предлагает ручные операции со сменой профиля.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** OEM-профиль и регистрация — отдельные измерения; обновление carrier profile не может быть автоматическим.

#### `4PDA-PRIMARY-MM-P11` — MM-P11 — a router maker's firmware AX3000

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1080524&st=400) · 25.09.2024 · offline `a router maker's firmware_AX3000___обсуждение__OpenWrt_.html:printpost[406]` · source `a router maker's firmware_AX3000___обсуждение__OpenWrt_.html` · whole-source-HTML SHA256 `48983793b5050d93f1a3dd9d12f177de03640a4a668d4996ffd62bd5a576682e`

**Reported observation:** a router maker's firmware AX3000 with DW5821e registered LTE but did not bring up a usable data bearer.

**Outcome limitation:** First 501 printposts only; see primary HTML and research extract for reported result.

**MedveModem implication (derived, not a statement by the author):** Distinguish registered/network-data state.

#### `4PDA-PRIMARY-MM-P13` — MM-P13 — Cudy TR3000

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1099628&st=280) · 26.02.2025 · offline `Cudy_TR3000__Cudy_TR30__AX3000_Travel_Router___стандартная_прошивка_и_OpenWRT____Обсуждение.html:printpost[299]` · source `Cudy_TR3000__Cudy_TR30__AX3000_Travel_Router___стандартная_прошивка_и_OpenWRT____Обсуждение.html` · whole-source-HTML SHA256 `4cbf6a82d2c758e0a0018e1db7b3035ba14c239c19fd35d014f25fbcc889b0cb`

**Reported observation:** Cudy TR3000 reports T77W968 success and other external modem incompatibilities.

**Outcome limitation:** First 501 printposts only; see primary HTML and research extract for reported result.

**MedveModem implication (derived, not a statement by the author):** Keep host/protocol compatibility scoped to device build.

### foxconn-t99w175 (14)

#### `4PDA-1086317-291` — T99W175/DW5930

**Classification:** `community-guide` · **risk:** `danger` · **integration:** `corroborates-existing-general-rule` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1086317) · 30.03.24, 22:42 · offline `4pda-four-topics/1086317/part-001.html#message-291` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `20fce88dddb656b012959568a64ff5772f2954a82d8435c3cef75fd35c7c0b03`

**Reported observation:** Закреплённая инструкция указывает, что CUSTOMER 3/7/9/10/11/13/15/17/22/24/26/30/31/35 на F0.6.0.0.6.GC.004 047 способен вызвать циклическую перезагрузку; отдельно предупреждение, что инструкция не для REV.3 V305 USB3.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Внести blacklist опасных CUSTOMER с привязкой FW; любые изменения CUSTOMER только с ручным подтверждением.

#### `4PDA-1086317-8180` — T99W175 Lenovo/OEM

**Classification:** `field-reported-fix` · **risk:** `write` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1086317) · 24.12.25, 08:48 · offline `4pda-four-topics/1086317/part-017.html#message-8180` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `1e4e2e653b5bffec8a63c6ed30ed3b8302e212bd811acbe9e4bb7e62e5a9a605`

**Reported observation:** На сборке T99W175.F0.6.0.0.6.GC.004 025 автор перевёл CUSTOMER 18 + 90D5 в CUSTOMER 5 + 9025, после чего a router maker's firmware заработал; аппаратный pin3 модифицировать не пришлось.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Физическая доработка не является обязательной для всех адаптеров и ревизий; фиксировать адаптер/пины в профиле отдельно.

#### `4PDA-1086317-8254` — T99W175 OEM VZ/GC

**Classification:** `partial-result` · **risk:** `write` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1086317) · 29.12.25, 17:39 · offline `4pda-four-topics/1086317/part-017.html#message-8254` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `18e8907079290c651bdf5e7638d3a03a27a093b6d88b43a8588b4936d6039c6b`

**Reported observation:** После использования 9025 пользователь увидел в ATI переход идентификаторов Qualcomm T99W175 VZ.010 → Thales MV31-W GC.004; a router maker's firmware заработал, но B7+B3 CA не собиралась.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Не выводить идентичность firmware только из ATI manufacturer; композиция и OEM-профиль могут менять строки идентификации. Не считать CA исправленной.

#### `4PDA-1086317-1562` — T99W175 GC.004

**Classification:** `field-reported-fix` · **risk:** `write` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1086317) · 06.06.24, 18:04 · offline `4pda-four-topics/1086317/part-004.html#message-1562` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `fa9dc1498bda87d0886b648a1c5babf1e7ef26c13e440ffc732a12e987ee4d3b`

**Reported observation:** На other scripts ZBT-WG1608-32M-GO2024-05-30-AB23 пользователь сообщил улучшение отображения сети/температуры с CUSTOMER16/USBSWITCH00B3 и установленными kmod-usb-net-cdc-mbim, libmbim, umbim; часть пакетов названа необязательной.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Отделить системные зависимости MBIM от сопутствующих приложений; фиксировать FW роутера.

#### `4PDA-1086317-7316` — T99W175 Rev.0 v065

**Classification:** `field-reported-fix` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1086317) · 30.09.25, 13:17 · offline `4pda-four-topics/1086317/part-015.html#message-7316` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `4c4b300f89b5d988756caddbaf7364bb95ae96b27eb924c77206b080f63d3c49`

**Reported observation:** После неудачных прошивок и отсутствия сети автор восстановил устройство через EDL/fastboot. Сообщил, что запись раздела sec поменяла USB VID:PID, потребовалось повторно сопоставлять WinUSB; далее fastboot обнаруживался как 18d1:d00d.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Этап восстановления может менять USB-identity несколько раз. Ни EDL-запись разделов, ни работа с boot/recovery не являются автоматическим recovery.

#### `4PDA-1086317-7009` — T99W175 GC.004

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1086317) · 27.08.25, 10:24 · offline `4pda-four-topics/1086317/part-015.html#message-7009` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `547ef4cf961b67ec187fb5f5ac9438f5727e6957ef50ea51d757697b3b479005`

**Reported observation:** После переключения USB/firmware пользователь сообщает скорость ниже 5 Мбит/с даже возле БС при наличии LTE B3+B7 по AT^CA_INFO. Причина не установлена.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Не путать CA presence и пропускную способность; проверять USB link speed, throttling, bearer/RF отдельно.

#### `4PDA-1086317-6822` — T99W175 Lenovo

**Classification:** `field-symptom` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1086317) · 06.08.25, 16:54 · offline `4pda-four-topics/1086317/part-014.html#message-6822` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `29615f7990940a448bc6f378c7155c849f3b9afe0a301bbdd9437d67258f3518`

**Reported observation:** После перехода с F0.0.5.7.GC.004 076 на F0.6.0.0.6 056 пользователь сообщил о нестабильном ping-check в a router maker's firmware; параллельно выполнены изменения carrier_policy/EFS, поэтому причинность не установлена.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Собирать полный change-set, запрет автоматических EFS-патчей.

#### `4PDA-1086317-5961` — T99W175 / Netis N6

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1086317) · 09.06.25, 23:51 · offline `4pda-four-topics/1086317/part-012.html#message-5961` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `58a3af05e1735004ba1149944a9ceca976c31ffe60f2c6d8f8be02d76fc77f16`

**Reported observation:** При композиции 9025 пользователь установил одновременно QMI, MBIM и ModemManager пакеты на OpenWrt 24.10.1, но сообщение описывает нерешённую проблему запуска.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Не считать просто наличие пакетов доказательством протокола и портов; resolver должен выбрать одного владельца.

#### `4PDA-1086317-7` — T99W175

**Classification:** `negative-result` · **risk:** `read` · **integration:** `corroborates-existing-general-rule` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1086317) · 18.03.24, 12:39 · offline `4pda-four-topics/1086317/part-001.html#message-7` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `b4da78da5c7d13527ad604ce34abbc6c201ebcbb40ab4b7460769c7de71728cb`

**Reported observation:** Пользователь прямо сообщил, что у него Windows 10/11 заработала только с 00B3, вопреки рекомендации 90D5; также упомянул условный фикс после прошивки, если не видит сеть.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Не устанавливать глобальную «лучшую» композицию на весь T99W175.

#### `4PDA-1102595-238` — WH3000 Pro/T99W175

**Classification:** `community-guide` · **risk:** `write` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1102595) · 18.07.25, 07:17 · offline `4pda-four-topics/1102595/part-001.html#message-238` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `1e11678979d9ec311cea538eeee11a1973cc8634da36cf52878099651d4af6d6`

**Reported observation:** Сборка a43 OpenWrt 24.10.3: для 05c6:90d5 (MBIM) автор вручную добавлял binding serial generic, 10-секундную паузу и последующий ifup modem2/modem3. Отдельно подчёркнуто, что подобная конфигурация не универсальна.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Сдвигать bring-up до готовности портов без фиксированного sleep для всех моделей; не делать global new_id без валидации состава интерфейсов.

#### `4PDA-1102595-1564` — WH3000 Pro/T99W175

**Classification:** `partial-result` · **risk:** `write` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1102595) · 01.06.26, 18:35 · offline `4pda-four-topics/1102595/part-004.html#message-1564` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `1bc5903e8b2f0f9d14d4707b71a338d7886bf3bb378c1e728e41273006d16303`

**Reported observation:** На OpenWrt 25.12.4 и T99W175 056 пользователь запустил сеть с пересозданным интерфейсом, но ttyUSB перемещались между 2,4,0; применял binding 05c6:90d5 для serial.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Привязка по sysfs parent USB, а не ttyUSB index; тест реконфигурации портов после reboot.

#### `4PDA-1102595-1435` — WH3000 Pro/T99W175

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1102595) · 05.05.26, 01:51 · offline `4pda-four-topics/1102595/part-003.html#message-1435` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `39f3502e61a963f79e0624a56573cce5672426016e1e483f9898163a5847b7df`

**Reported observation:** На OpenWrt 25.12.2 автор получил MM_3GPP_OPERATOR_REGISTRATION_FAILED и MM_INIT_EPS_BEARER_SET_FAILED при наличии cdc_mbim/option; изменение APN и PAP не помогло.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Разделять наличие MBIM драйвера, регистрацию PLMN, PDP bearer и доступность маршрута; не считать APN единственной причиной.

#### `4PDA-PRIMARY-MM-P04` — MM-P04 — Huasifei WH3000 / Pro

**Classification:** `field-observation` · **risk:** `write` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1102595&st=220) · 18.07.2025 · offline `Huasifei_WH3000__Huasifei_WH3000_Pro__OpenWRT____обсуждение.html:printpost[237]` · source `Huasifei_WH3000__Huasifei_WH3000_Pro__OpenWRT____обсуждение.html` · whole-source-HTML SHA256 `9ec6798320632e9ff3707f49b32decf65d33ecf75e6baf91c686e1fca76c28d4`

**Reported observation:** WH3000 Pro T99W175 05c6:90d5 MBIM exposes no serial port without special binding in one build.

**Outcome limitation:** First 501 printposts only; see primary HTML and research extract for reported result.

**MedveModem implication (derived, not a statement by the author):** Never assume ttyUSB; binding must be model/interface-scoped and operator-approved.

#### `4PDA-PRIMARY-MM-P06` — MM-P06 — Huasifei WH3000 / Pro

**Classification:** `field-observation` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1102595&st=300) · 24.07.2025 · offline `Huasifei_WH3000__Huasifei_WH3000_Pro__OpenWRT____обсуждение.html:printpost[303]` · source `Huasifei_WH3000__Huasifei_WH3000_Pro__OpenWRT____обсуждение.html` · whole-source-HTML SHA256 `9ec6798320632e9ff3707f49b32decf65d33ecf75e6baf91c686e1fca76c28d4`

**Reported observation:** WH3000 T99W175 reported incompatibilities among CUSTOMER, USB switch and OEM firmware branches.

**Outcome limitation:** First 501 printposts only; see primary HTML and research extract for reported result.

**MedveModem implication (derived, not a statement by the author):** CUSTOMER/USBSWITCH are manual service-only operations.

### generic (15)

#### `4PDA-1125602-1` — T99W640/DW5934e

**Classification:** `community-guide` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1125602) · 27.08.26, 21:07 · offline `4pda-four-topics/1125602/part-001.html#message-1` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `03aab117f85a5a8fb91a1696d42f31bea00c2c9f495eb31bc81a47b00d2dac01`

**Reported observation:** В шапке специализированной темы предупреждение: T99W640 Snapdragon X72 не работает через USB из коробки; устройство рассчитано на PCIe M.2, также перечислены OEM firmware.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Не применять автоматически USB-профили T99W175 к T99W640.

#### `4PDA-1125602-59` — T99W640/DW5934e

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1125602) · 02.10.26, 21:49 · offline `4pda-four-topics/1125602/part-001.html#message-59` · source `4pda-four-topics-no-images.zip` · message-text SHA256 `ebba7464e55b8e0910090b2750bef97fca3914603ae74595978f0fc2b2828615`

**Reported observation:** Разработчик экспериментального FCC unlock описывает deadlock firmware при reject чужой БС; временно держит периодический ping, но считает обход неудовлетворительным. Работа через /dev/wwan0mbim0, qmicli --device-open-mbim.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Класс отдельный от USB reset: сеть/firmware deadlock после reject, не бесконечные power-cycles; пинг не признать универсальным фиксом.

#### `4PDA-994474-7334` — Quectel / Mikrotik

**Classification:** `field-reported-fix` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 13.03.21, 08:57 · offline `4pda-994474-html/part-015.html#message-7334` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `a772b91b53726b57a229c5463c87ac4ff76d09af4940461cfe0f1fd2d642a63d`

**Reported observation:** После добавления конфигурационных файлов пользователь увидел PCC B7 и SCC B3 в AT+QCAINFO; GUI WinBox ранее вводил в заблуждение.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Телеметрию CA строить по ответу модема, а не по переключателю/выводу интерфейса.

#### `4PDA-994474-7485` — Foxconn T77W676

**Classification:** `partial-result` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 16.03.21, 13:45 · offline `4pda-994474-html/part-015.html#message-7485` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `7ea230c9f89d23ce5a7fe60e7d155cb1e537c3f57aeeaff9bedac0b71d1a6af3`

**Reported observation:** На Xiaomi Mi WiFi Router 3G/other scripts после перехода MBIM→QMI заработала связь, AT^CA_INFO показал B3 15 MHz + B20 5 MHz. Автор также описал отказ запуска портов после обесточивания USB-хаба, восстановление иногда требовало повторного power-cycle.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Сохранять отдельно CA telemetry и аппаратно-USB аварийные события; одинаковый симптом в разных протоколах.

#### `4PDA-994474-3699` — Несколько OEM/Quectel/Fibocom

**Classification:** `community-guide` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 05.11.20, 23:00 · offline `4pda-994474-html/part-008.html#message-3699` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `58c65354351c8470bb43a71a033ece2c118d08414dfb2cdcfe88c862559158cc`

**Reported observation:** Кураторская инструкция по MikroTik перечисляет множество M.2 модемов и различия USB/MBIM/ECM, отдельно предостерегает от «прошить ради новой версии».

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Считать как сводный community guide, не лабораторный тест для всех перечисленных модемов.

#### `4PDA-994474-5363` — HP LT4220/T77W676

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 21.01.21, 13:48 · offline `4pda-994474-html/part-011.html#message-5363` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `7a530e0af47f06d30ae329308e86d421772f51d9cefa7adb93321c7a1e39b839`

**Reported observation:** В сравнительном опыте пользователь не смог подключить модем по длинным USB-кабелям (2–3 м), но другой монтаж работал; использовал измеритель питания.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Помечать топологию, кабель, USB-удлинитель и питание как условия наблюдения.

#### `4PDA-PRIMARY-MM-P03` — MM-P03 — ZBT-WE3926

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=935012&st=60) · 29.10.2019 · offline `ZBT-WE3926___обсуждение__intelligent_router_.html:printpost[74]` · source `ZBT-WE3926___обсуждение__intelligent_router_.html` · whole-source-HTML SHA256 `7f0a278b8e6c6b6557d5302c27600e5434df54a45fdcece9894d59497844b795`

**Reported observation:** WE3926 historic ping-based reconnect/reset advice can lead to continuous resets despite unrelated DNS/routing failures.

**Outcome limitation:** First 501 printposts only; see primary HTML and research extract for reported result.

**MedveModem implication (derived, not a statement by the author):** Bound resets; diagnose network, DNS and PDP independently.

#### `4PDA-PRIMARY-MM-P07` — MM-P07 — Huasifei WH3000 / Pro

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1102595&st=340) · 02.08.2025 · offline `Huasifei_WH3000__Huasifei_WH3000_Pro__OpenWRT____обсуждение.html:printpost[343]` · source `Huasifei_WH3000__Huasifei_WH3000_Pro__OpenWRT____обсуждение.html` · whole-source-HTML SHA256 `9ec6798320632e9ff3707f49b32decf65d33ecf75e6baf91c686e1fca76c28d4`

**Reported observation:** WH3000 reboot loop diagnosed as faulty/inadequate power supply in one user report.

**Outcome limitation:** First 501 printposts only; see primary HTML and research extract for reported result.

**MedveModem implication (derived, not a statement by the author):** Host-power diagnostic before attributing repeated USB disconnects to modem firmware.

#### `4PDA-PRIMARY-MM-P08` — MM-P08 — ZBT Z8102AX Dual SIM

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1110636&st=380) · 21.05.2026 · offline `ZBT_Z8102AX_Dual_SIM___обсуждение__a router maker's firmware_OS_4_3_2_.html:printpost[386]` · source `ZBT_Z8102AX_Dual_SIM___обсуждение__a router maker's firmware_OS_4_3_2_.html` · whole-source-HTML SHA256 `b9343e21f75d2a850e6311b3582e9ce13baca8c42139396d0f10390ccfb29ce3`

**Reported observation:** Z8102AX V2 running FM350 RNDIS via XMM and T99W175 via ModemManager with MWAN3; backend conflict resolved by owner separation.

**Outcome limitation:** First 501 printposts only; see primary HTML and research extract for reported result.

**MedveModem implication (derived, not a statement by the author):** Per-modem owner, topology-aware interfaces; no competing MM/XMM.

#### `4PDA-PRIMARY-MM-P09` — MM-P09 — ZBT Z8102AX Dual SIM

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1110636&st=260) · 13.01.2026 · offline `ZBT_Z8102AX_Dual_SIM___обсуждение__a router maker's firmware_OS_4_3_2_.html:printpost[264]` · source `ZBT_Z8102AX_Dual_SIM___обсуждение__a router maker's firmware_OS_4_3_2_.html` · whole-source-HTML SHA256 `b9343e21f75d2a850e6311b3582e9ce13baca8c42139396d0f10390ccfb29ce3`

**Reported observation:** Z8102AX V2 modem behaviour varied with host firmware and USB2/USB3 negotiation.

**Outcome limitation:** First 501 printposts only; see primary HTML and research extract for reported result.

**MedveModem implication (derived, not a statement by the author):** Correlate regression with USB host driver and router firmware.

#### `4PDA-PRIMARY-MM-P10` — MM-P10 — ZBT Z8102AX Dual SIM

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1110636&st=480) · 20.08.2026 · offline `ZBT_Z8102AX_Dual_SIM___обсуждение__a router maker's firmware_OS_4_3_2_.html:printpost[493]` · source `ZBT_Z8102AX_Dual_SIM___обсуждение__a router maker's firmware_OS_4_3_2_.html` · whole-source-HTML SHA256 `b9343e21f75d2a850e6311b3582e9ce13baca8c42139396d0f10390ccfb29ce3`

**Reported observation:** Z8102AX reported limits to automatic modem-service startup and co-existence between managers.

**Outcome limitation:** First 501 printposts only; see primary HTML and research extract for reported result.

**MedveModem implication (derived, not a statement by the author):** Lifecycle arbitration and attachment re-discovery mandatory.

#### `4PDA-PRIMARY-MM-P12` — MM-P12 — netis N6

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1065564&st=360) · 09.03.2024 · offline `netis_N6___обсуждение.html:printpost[363]` · source `netis_N6___обсуждение.html` · whole-source-HTML SHA256 `d78418d5ff02e0c34d9c03cd52466c9882c4bc92ada05172cc4b2df312b31435`

**Reported observation:** Netis N6 with HiLink and USB power/hub constraints: stability depends on host hardware as well as modem protocol.

**Outcome limitation:** First 501 printposts only; see primary HTML and research extract for reported result.

**MedveModem implication (derived, not a statement by the author):** Measure USB power and isolate USB host vs cellular failures.

#### `4PDA-PRIMARY-MM-P14` — MM-P14 — Cudy TR3000

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1099628&st=120) · 07.02.2025 · offline `Cudy_TR3000__Cudy_TR30__AX3000_Travel_Router___стандартная_прошивка_и_OpenWRT____Обсуждение.html:printpost[126]` · source `Cudy_TR3000__Cudy_TR30__AX3000_Travel_Router___стандартная_прошивка_и_OpenWRT____Обсуждение.html` · whole-source-HTML SHA256 `4cbf6a82d2c758e0a0018e1db7b3035ba14c239c19fd35d014f25fbcc889b0cb`

**Reported observation:** Cudy TR3000 USB3 failures can originate in host controller and do not imply cellular radio problem.

**Outcome limitation:** First 501 printposts only; see primary HTML and research extract for reported result.

**MedveModem implication (derived, not a statement by the author):** Distinguish USB enumeration and data-channel recovery.

#### `4PDA-PRIMARY-MM-P17` — MM-P17 — a router maker's firmware Omni

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=775914&st=20) · 16.04.2017 · offline `ZyXEL___a router maker's firmware_Omni_-_Обсуждение.html:printpost[23]` · source `ZyXEL___a router maker's firmware_Omni_-_Обсуждение.html` · whole-source-HTML SHA256 `7a9489fac788151827074afe9801b6daf79e13eac9b3312180b44c9ed95ccf05`

**Reported observation:** a router maker's firmware Omni II reported slow modem startup and firmware/USB-hub-dependent behaviour.

**Outcome limitation:** First 501 printposts only; see primary HTML and research extract for reported result.

**MedveModem implication (derived, not a statement by the author):** Wait on USB readiness and prevent aggressive session reset.

#### `4PDA-PRIMARY-MM-P18` — MM-P18 — a router maker's firmware Viva

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=929843&st=60) · 31.01.2019 · offline `a router maker's firmware_Viva__KN-1910__KN-1912__KN-1913__-_Обсуждение.html:printpost[65]` · source `a router maker's firmware_Viva__KN-1910__KN-1912__KN-1913__-_Обсуждение.html` · whole-source-HTML SHA256 `93ac627418b5985d348f2acd70661d56a67160e6cc6da2616b5af409b9e7b46e`

**Reported observation:** a router maker's firmware Viva two USB routers/modems can expose overlapping private subnets causing WAN route conflicts.

**Outcome limitation:** First 501 printposts only; see primary HTML and research extract for reported result.

**MedveModem implication (derived, not a statement by the author):** Distinguish two HiLink NAT gateways; per-attachment route and subnet isolation.

### huawei-hilink (1)

#### `4PDA-PRIMARY-MM-P16` — MM-P16 — Huawei E3372

**Classification:** `community-guide` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=582284) · 02.09.2014 · offline `Huawei_E3372__МТС_827F_829F__МегаФон_M150-2__Билайн_E3372_E3370__TELE2_E3372h-153__-_Обсуждение.html:printpost[2]` · source `Huawei_E3372__МТС_827F_829F__МегаФон_M150-2__Билайн_E3372_E3370__TELE2_E3372h-153__-_Обсуждение.html` · whole-source-HTML SHA256 `9fd8752e478633d8352870a483272a58737267e8f86a52780e5132701568db5f`

**Reported observation:** Huawei E3372 Stick vs HiLink/flash mode depends on precise variant and firmware.

**Outcome limitation:** First 501 printposts only; see primary HTML and research extract for reported result.

**MedveModem implication (derived, not a statement by the author):** No blanket HiLink-to-Stick command migration.

### quectel-lte-ec25-ep06-em12 (6)

#### `4PDA-994474-1530` — Quectel EP06-E

**Classification:** `partial-result` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 29.08.20, 17:15 · offline `4pda-994474-html/part-004.html#message-1530` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `34e479c3a9974637f47bfa4e7e67c6ebcf1d9bc3860bb733779afe7d5a84a24d`

**Reported observation:** На Mikrotik после некоторого времени пропадает B7+B7 CA; AT+QCAINFO возвращает только OK, ручной restart LTE-interface временно помогает. Сообщено для MBIM и QMI.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Вводить отдельный статус CA lost (без reboot); отслеживать контекст RRC/нагрузка и оценивать soft reconnect до reset.

#### `4PDA-994474-32204` — Quectel EP06-E

**Classification:** `partial-result` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 31.12.22, 13:26 · offline `4pda-994474-html/part-065.html#message-32204` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `cb7c36a436ef27500bbc775296aa925990e282e47d75c8dd31d2e427e22f1ffd`

**Reported observation:** После обновления firmware 04→05/06 автор сообщил исчезновение B3+B7 CA; возврат к 04 дал PCC B3 + SCC B7 и B7+B7 в последующих пробах. Комбинация и скорость менялись со временем.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Откат FW не автоматизировать. Логировать действующие PCC/SCC, SINR, bandwidth и firmware до/после, а не «поддерживаемые бэнды».

#### `4PDA-994474-9324` — Quectel EM12-G / MikroTik

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 28.04.21, 21:27 · offline `4pda-994474-html/part-019.html#message-9324` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `64e479df6023d25af057ef18989d91a03a418719e0f9b5ca5d1c590efb397409`

**Reported observation:** Mikrotik RBM33G/RouterOS 7.1beta5, EM12-G EM12GPAR01A20M4G, MBIM usbnet=2: `lte1 mbim: error: wdm send failed: 19`, AT-chat висит, интерфейс пропадает через минуты/дни; автор не нашёл решения.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Ошибка WDM и потеря USB интерфейса — уровень транспорта, а не обязательно радиосети; нет подтверждённого фикса.

#### `4PDA-994474-8569` — Quectel EP06

**Classification:** `field-symptom` · **risk:** `write` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 08.04.21, 19:42 · offline `4pda-994474-html/part-018.html#message-8569` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `3fc16ae50b7ced5ae835fbe19f39a39f9e4803819cf5d3f33f754bc4e6a477e5`

**Reported observation:** После смены EP06 на MBIM `AT+QCFG="usbnet",2` модем перестал отвечать через YAT, а Mikrotik не увидел data interface.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** После смены режима прогнозировать изменение USB-узлов и доступности AT, recovery по USB topology.

#### `4PDA-994474-20990` — Quectel EP06-E

**Classification:** `partial-result` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 27.02.22, 21:56 · offline `4pda-994474-html/part-042.html#message-20990` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `12dbd105cebe2c81b19cfcf97470449689e6ea9d9495b2331f67b5234f5b43a3`

**Reported observation:** Автор измерил B7+B3 через QCAINFO и 60/20→93/20 после FW+256QAM, но после отключения питания получил 43/30 и QCAINFO только OK.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Не приписывать прирост единственной команде при одновременной прошивке, изменении соты и 256QAM; telemetry needs timestamps.

#### `4PDA-PRIMARY-MM-P02` — MM-P02 — ZBT-WE3926

**Classification:** `community-guide` · **risk:** `write` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=935012&st=80) · 11.11.2019 · offline `ZBT-WE3926___обсуждение__intelligent_router_.html:printpost[82]` · source `ZBT-WE3926___обсуждение__intelligent_router_.html` · whole-source-HTML SHA256 `7f0a278b8e6c6b6557d5302c27600e5434df54a45fdcece9894d59497844b795`

**Reported observation:** WE3926: EC25/SIM7600/Telit QMI bring-up with cdc-wdm, uqmi, kmod and luci-proto-qmi; real wwan0 address shown.

**Outcome limitation:** First 501 printposts only; see primary HTML and research extract for reported result.

**MedveModem implication (derived, not a statement by the author):** Gate package/proto prerequisites and resolve cdc-wdm by USB parent.

### quectel-rm520n (27)

#### `4PDA-994474-40274` — Quectel RM520N-GL

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 01.07.23, 09:46 · offline `4pda-994474-html/part-081.html#message-40274` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `9363a9ce09187f594a877ca8c93293bbb3980705cbc698d9377eb2478a38b182`

**Reported observation:** Два SunComm M12K21 с RM520NGLAAR01A06M4G наблюдали БС, но не регистрировались (AT+CREG? 0,0); автор подозревал региональные ограничения и уточнял release notes, исход не известен.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Не диагностировать «плохой APN» до регистрации; учитывать регион/OEM/FW, но не утверждать блокировку без факта.

#### `4PDA-994474-53406` — Quectel RM520N-GL

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 28.11.24, 03:14 · offline `4pda-994474-html/part-107.html#message-53406` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `0408476f36b8132cb8c41f184e7f633300e2bf49980b44d27fc9acb72f94807a`

**Reported observation:** После прошивки RM520NGLAAR01A06M4G_01.001.01.001 пользователь видит CFUN:5, +CME ERROR phone failure при CFUN=1, сеть не регистрируется; выхода из FTM в сообщении нет.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Раздельный статус RF/FTM, прекращение бессмысленного APN retry, запрет автоматической FTM→online последовательности без профиля.

#### `4PDA-994474-45271` — Quectel RM520N (FW 07)

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 07.12.23, 10:58 · offline `4pda-994474-html/part-091.html#message-45271` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `3f52887a68714ef3ce47a5061ee0ededbff37eb4a8d7751ac9b839b33338c21f`

**Reported observation:** После работы через OpenWrt QMI, затем RouterOS MBIM и возврата к OpenWrt пользователь столкнулся с SIM not registered; решение не приведено.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** Проверять сохранённый USBNET, менеджера, APN и регистрационное состояние; не перезаписывать QCFG только по прошлой конфигурации.

#### `4PDA-994474-56904` — Quectel RM520N-GL

**Classification:** `partial-result` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474) · 29.07.25, 14:11 · offline `4pda-994474-html/part-114.html#message-56904` · source `4pda-994474-html-no-images.zip` · message-text SHA256 `ed76f57dd020a1b5cb3ae236c26428402e44f6183650c5e1a98746a48fff896b`

**Reported observation:** Пользователь приводит набор команд из форума производителя для RJ45/PCIe режима и сообщает, что соединение заработало, но позже сильно упала скорость.

**Outcome limitation:** See primary archived post; result class is a reported outcome, not independent replication.

**MedveModem implication (derived, not a statement by the author):** PCIE/QETH/QMAPWAC — отдельный режим data path. Не копировать изменение `data_interface`, `pcie/mode` в USB-модемные профили.

#### `4PDA-1106442-1` — Quectel RM520N-GL

**Classification:** `community-guide` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 14.05.25, 03:23 · offline `4pda-1106442-1066668/1106442/part-001.html#message-1` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `88d7a47ec04413c026f14fbf8f1832ded91ebf0dcac6abb804fd896d8d23776b`

**Reported observation:** Шапка темы утверждает, что на последних версиях прошивок модем не работает в РФ.

**Outcome limitation:** Шапка не специфицирует все новые ветки и аппаратные варианты.

**MedveModem implication (derived, not a statement by the author):** Записывать географию/оператора и FW; не делать универсальный вывод по всем RM520N.

#### `4PDA-1106442-2` — Quectel RM520N-GL AA / AP

**Classification:** `field-observation` · **risk:** `read` · **integration:** `corroborates-existing-general-rule` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 14.05.25, 10:09 · offline `4pda-1106442-1066668/1106442/part-001.html#message-2` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `8eb62d18d98733907c71a45aafa34d89038718f62c833bf9c7da9fa033359de2`

**Reported observation:** Автор сообщает AA поддерживает USB и PCIe; AP — только PCIe.

**Outcome limitation:** OEM/адаптеры требуется проверять по реальному физическому интерфейсу, не этикетке.

**MedveModem implication (derived, not a statement by the author):** Separate `usb`/`pcie_mhi` transport profile, no phantom USB probing on AP.

#### `4PDA-1106442-599` — Quectel RM520N-GL AA

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 05.07.25, 23:43 · offline `4pda-1106442-1066668/1106442/part-002.html#message-599` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `c2de0d9f408c8507bc823902000458830b0f3b94497cc957c941fdb96426a455`

**Reported observation:** AA FW RM520NGLAAR01A06M4G_01.001.01.001: USB QMI+MBIM работают в other scripts RBM33G; MBIM работает в Windows11 и RouterOS 7.19.2.

**Outcome limitation:** На MikroTik видны CA/ULCA/MIMO; PCIe не запустился, автор подозревает адаптер.

**MedveModem implication (derived, not a statement by the author):** Поддержка зависит от host/protocol; failure PCIe не считать доказательством отсутствия PCIe в AA.

#### `4PDA-1106442-690` — Quectel RM520N-GL

**Classification:** `community-guide` · **risk:** `danger` · **integration:** `corroborates-existing-general-rule` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 12.07.25, 18:26 · offline `4pda-1106442-1066668/1106442/part-002.html#message-690` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `c7b8ab4c69d77c92e4856cebbd5431d66b34f6b00b6d65404a9113752c791d4c`

**Reported observation:** ADB обследование показало IDs 905B MBIM, 90D6 DIAG+MBIM+GNSS+DUN, 9025 DIAG+ADB+MODEM+NMEA+QMI_RMNET, 90B1 ECM, 4EE7 ADB, F000 Mass Storage и многие другие.

**Outcome limitation:** Автор предупреждает не менять без причины; перечислены возможные функции, не доказано что все реально пригодны.

**MedveModem implication (derived, not a statement by the author):** Treat as observational PID map with low confidence for activation; deny auto switch.

#### `4PDA-1106442-706` — Quectel RM520N-GL AA / a router maker's firmware Giga

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 15.07.25, 22:56 · offline `4pda-1106442-1066668/1106442/part-002.html#message-706` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `3f0fdc9f20da6545bd9c06095dc53704e883eebe7b621ca8f7f403ad3f7eb776`

**Reported observation:** USB QMI (usbnet=0), adapter Vertell, a router maker's firmware Giga KN-1012 / a router maker's firmwareOS 4.3.4 сразу распознал модем, LTE CA B1+B3, NR band controls.

**Outcome limitation:** Автор измерил UL ~30 Мбит/с; на MikroTik в MBIM ~60 Мбит/с, точная причина не доказана.

**MedveModem implication (derived, not a statement by the author):** Protocol-specific performance and CA metrics; avoid asserting QMI limits ULCA.

#### `4PDA-1106442-718` — Quectel RM520N-GL / pfSense

**Classification:** `field-observation` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 16.07.25, 23:36 · offline `4pda-1106442-1066668/1106442/part-002.html#message-718` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `370866a302ee43c0d53279aea27f847f396ed05cd0e6a40e6cdaeb56c7824945`

**Reported observation:** Пользователь в Европе вывел RM520N по Ethernet на pfSense и сообщил ~600 Мбит/с; в сравнении FM350 на OpenWrt ~120 Мбит/с.

**Outcome limitation:** Скорость ограничена оператором, антенны и host отличались.

**MedveModem implication (derived, not a statement by the author):** Ethernet path as separate network device, not USB QMI instance.

#### `4PDA-1106442-713` — Quectel RM520N-GL / a router maker's firmware Ultra

**Classification:** `negative-result` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 16.07.25, 16:24 · offline `4pda-1106442-1066668/1106442/part-002.html#message-713` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `cdb60a24efdef2b00b10c10ec45e185b5e36fd61bbf443dde9f4ed72a97dea9e`

**Reported observation:** В одном случае USB3 дал до ~160 Мбит/с, переход на Ethernet адаптер с 1Gbps link резко снизил скорость.

**Outcome limitation:** Проблема оставалась открытой в сообщении.

**MedveModem implication (derived, not a statement by the author):** Do not infer good internet from PHY link speed; trace bridge/NAT/QMAP/routing.

#### `4PDA-1106442-93` — Quectel RM520N-GL / RTL8125

**Classification:** `partial-result` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 28.05.25, 19:11 · offline `4pda-1106442-1066668/1106442/part-001.html#message-93` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `56896c66719242a62de4abe34071217f0cfc2b5e953381733fd89c8a3aa6a6a7`

**Reported observation:** После переходов RJ45↔QMI связь не поднималась, хотя a router maker's firmware распознавал модем.

**Outcome limitation:** Ручное включение/выключение MPDN_RULE временно помогало до следующего reboot.

**MedveModem implication (derived, not a statement by the author):** Capture QMAP/MPDN route state; no blind usbnet toggling in failover.

#### `4PDA-1106442-1101` — Quectel RM520N-GL AA

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 01.10.25, 17:29 · offline `4pda-1106442-1066668/1106442/part-003.html#message-1101` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `cff872a0fc6ed2ca3d70af47961692d72363dc734ee62ac7c78b5256d2d819dc`

**Reported observation:** FW RM520NGLAAR01A06M4G_01.001.01.001: после cold boot модем остаётся в CFUN=0, вручную CFUN=1 исправляет, mode_pref сам возвращается AUTO с LTE.

**Outcome limitation:** Причина не выяснена; последующий ответ 1106 содержит предложения, не подтвержденный fix.

**MedveModem implication (derived, not a statement by the author):** On boot inspect CFUN and actual RAT preference, distinguish radio-off vs network failure.

#### `4PDA-1106442-1106` — Quectel RM520N-GL

**Classification:** `community-guide` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 03.10.25, 00:25 · offline `4pda-1106442-1066668/1106442/part-003.html#message-1106` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `8eba1cc045ad819a1bd0ed2e1e0d331b6e664edbc21bc662bc7979874e6c9c31`

**Reported observation:** Совет перехода QMI→MBIM или записи `QNVFW` Thin_UI NVM, затем reboot, чтобы избежать low power state.

**Outcome limitation:** Автор исходного обращения не подтвердил результат.

**MedveModem implication (derived, not a statement by the author):** Не исполнять QNVFW или usbnet автоматически: hypothesis only.

#### `4PDA-1106442-1392` — Quectel RM520N-GL / a router maker's firmware

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 19.10.25, 12:40 · offline `4pda-1106442-1066668/1106442/part-003.html#message-1392` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `85990cdbce7af790e230e8d283720b5116037dc086f016b8b02098e4c44afcc6`

**Reported observation:** В R03 режиме моста через 3–5 суток a router maker's firmware терял internet, web UI модема тоже становился недоступен; восстанавливался power-cycle.

**Outcome limitation:** На R01 и в R03 без моста такого в опыте автора не наблюдалось.

**MedveModem implication (derived, not a statement by the author):** Long-soak bridge test, separate bridge firmware regression from radio registration.

#### `4PDA-1106442-1203` — Quectel RM520N-GL

**Classification:** `negative-result` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 11.10.25, 11:18 · offline `4pda-1106442-1066668/1106442/part-003.html#message-1203` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `d98a0766cd22699d7707c966657d6f07ef00c9581ea43a8290e178eb8dde8c6c`

**Reported observation:** Режим моста включён, но хост получал адрес 192.168.225.x от модема при конфигурации MPDN_RULE и r8125.

**Outcome limitation:** Настоящий passthrough не подтверждён.

**MedveModem implication (derived, not a statement by the author):** WAN addressing classifier must detect private DHCP/NAT even when GUI says bridge.

#### `4PDA-1106442-330` — Quectel RM520N-GL AA

**Classification:** `community-guide` · **risk:** `danger` · **integration:** `corroborates-existing-general-rule` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 20.06.25, 04:14 · offline `4pda-1106442-1066668/1106442/part-001.html#message-330` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `57a0ecde10e8a71db44e82b8783f65c8cd246f1fd13760bd5fcf6fc95ddcefe0`

**Reported observation:** Инструкция настаивает на собственном QCN/XQCN до прошивки и не рекомендует использовать чужой backup как замену заводского.

**Outcome limitation:** Связанные xqcn архивы отсутствуют в no-images ZIP.

**MedveModem implication (derived, not a statement by the author):** Provisioning gate: backup present, identity/NV/calibration snapshot; no QCN exchange in daemon.

#### `4PDA-1106442-351` — Quectel RM520N-GL AA

**Classification:** `field-reported-fix` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 20.06.25, 12:04 · offline `4pda-1106442-1066668/1106442/part-001.html#message-351` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `64cdb0320d8d56d997e6dec81dc2c555e7f482b7818d0faf5507c197011e921e`

**Reported observation:** Автор сообщил успешное восстановление работы после перехода R03 A03 → R01 A06 через QFlash и QCN restore.

**Outcome limitation:** Успех со второго раза в одной конфигурации, без гарантий на AP/OEM и другие FW.

**MedveModem implication (derived, not a statement by the author):** Recovery only supervised; record source FW/destination FW and QCN hash.

#### `4PDA-1106442-1938` — Quectel RM520N-GL AP

**Classification:** `field-reported-fix` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 13.04.26, 21:22 · offline `4pda-1106442-1066668/1106442/part-004.html#message-1938` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `787d73738c1f71ac9cc7fcaf643aa4954420e5c83f2b3c0b6e715f4e05d03168`

**Reported observation:** После прошивки AP инженерного образца на 001 аппарат был недоступен. Автор восстановил EDL/QFIL с erase-all и затем собственным backup; аналогичный опыт для HP.

**Outcome limitation:** Процедура уничтожает исходные разделы; не универсальна и сильно зависит от совместимого бэкапа.

**MedveModem implication (derived, not a statement by the author):** EDL/erase-all explicitly forbidden in unattended recovery.

#### `4PDA-1106442-2375` — Quectel RM520N-GL AA/AP

**Classification:** `field-symptom` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 12.06.26, 16:06 · offline `4pda-1106442-1066668/1106442/part-005.html#message-2375` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `06c6e13681fdee6e5543ab9f6b0cd70cbb6b5c9f65ec7246ffb3914c47a555b3`

**Reported observation:** AP после AT USB mode потерял AT доступ. Другой AA временно оживал после прошивки на ~2 дня, позже снова умер.

**Outcome limitation:** На момент сообщения оба случая не восстановлены.

**MedveModem implication (derived, not a statement by the author):** AT missing != no modem: differentiate PCIe/QDL/USB, no repeated flash looping.

#### `4PDA-1106442-2390` — Quectel RM520N-GL AA/AP

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 14.06.26, 18:13 · offline `4pda-1106442-1066668/1106442/part-005.html#message-2390` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `a4f528e7dda5cfdf6c79b5e7abd153bc8ae22bcdc115ff2a33970863393eb40d`

**Reported observation:** Автор увидел для AA `05c6:9008` в USB QDL; для AP `1eac:1004` QUSB_BULK; ручная установка Windows драйвера 9008 не дала работающего прошивальщика.

**Outcome limitation:** Только журнальные идентификаторы конкретных OEM-состояний.

**MedveModem implication (derived, not a statement by the author):** Do not classify 9008 as fully recovered; distinguish emergency enumerated vs flash transport alive.

#### `4PDA-1106442-2236` — Quectel RM520NGLAP-M20-CAF

**Classification:** `partial-result` · **risk:** `danger` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 25.05.26, 08:08 · offline `4pda-1106442-1066668/1106442/part-005.html#message-2236` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `d9d0d24347a787fd82a8c9356521af16f279352c676764b40592e2c0720e0d8f`

**Reported observation:** В AP OEM M20-CAF автор прочитал eUICC EID и установил профиль eSIM в Windows; восстановление разными чужими QCN по-разному влияло на регистрацию.

**Outcome limitation:** eSIM поддержана в этом конкретном варианте; восстановление NV нестабильно.

**MedveModem implication (derived, not a statement by the author):** Never infer eSIM presence from AP label alone; query hardware/eUICC safely; forbid foreign QCN.

#### `4PDA-1106442-2385` — Quectel RM520N-GL AP

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 14.06.26, 08:29 · offline `4pda-1106442-1066668/1106442/part-005.html#message-2385` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `993a2079683bf3b472691b5c37e1388e2f6d982d9d0555e8a58776734feaa0a2`

**Reported observation:** Linux PCI 17cb:0308 с mhi-pci-generic, mhi_wwan_mbim; присутствуют /dev/wwan0at0, /dev/wwan0mbim0, /dev/wwan0qcdm0; mmcli распознал RM520N-GL.

**Outcome limitation:** QMI на этой конфигурации отсутствует, MBIM работает на MHI.

**MedveModem implication (derived, not a statement by the author):** Implement PCIe/MHI discovery separate from cdc-wdm USB; AT endpoints differ from ttyUSB.

#### `4PDA-1106442-2836` — Quectel RM520NGLAP-M20-HP / ThinkPad

**Classification:** `field-reported-fix` · **risk:** `write` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 18.09.26, 12:42 · offline `4pda-1106442-1066668/1106442/part-006.html#message-2836` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `050ed40a0d4109eea0e93637b90671f477443450ee892d34500a57e4275dbbb8`

**Reported observation:** Ubuntu 26.04.1, PCI 1eac:1004 sub 1eac:3003, mhi-pci-generic; изначально AT+QSIMCFG="disable_physim" вернул 1,0 и CPIN CME10.

**Outcome limitation:** Автор сообщает, что включение физической SIM для UIM1 и полный cold boot привели к CPIN READY и рабочему LTE интернету.

**MedveModem implication (derived, not a statement by the author):** SIM absent can be OEM NV config, not bad card; cold-power-cycle after UIM change; supervised config.

#### `4PDA-1106442-1945` — Quectel RM520N-GL AP / OpenWrt LEDE

**Classification:** `field-symptom` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 20.04.26, 19:21 · offline `4pda-1106442-1066668/1106442/part-004.html#message-1945` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `39fb6260a787607a08b161642732d48e5bc1e9b361512fe64196bbea239b021a`

**Reported observation:** На R01 PCIe x86? Router LEDE обнаружил 1eac:1004, но не поднял modem driver; на R03 модель распознавалась иначе.

**Outcome limitation:** Проблема осталась открытой, лог приложен отдельно, не загружен в no-images ZIP.

**MedveModem implication (derived, not a statement by the author):** PCI enumerated != MHI/WWAN ready; driver/firmware compatibility matrix.

#### `4PDA-1106442-2378` — Quectel RM520N-GL

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 12.06.26, 18:30 · offline `4pda-1106442-1066668/1106442/part-005.html#message-2378` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `9b4f04c07db0c967d24d37f88e6574a30389f7e4f58a75512ae9639e1e26da2b`

**Reported observation:** В AT+QCAINFO автор получил PCC LTE B3 и SCC B1, B3, B7, B7 — одновременно пять LTE компонент.

**Outcome limitation:** Видны динамические PCC/SCC и bandwidth, без доказательства скорости.

**MedveModem implication (derived, not a statement by the author):** Parse variable number of SCC and band duplicates, snapshots not fixed supported CA.

#### `4PDA-1106442-2545` — Quectel RM520N-GL AP

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442) · 14.07.26, 07:29 · offline `4pda-1106442-1066668/1106442/part-006.html#message-2545` · source `4pda-1106442-1066668-no-images.zip` · message-text SHA256 `1bdeee166870297f350f90f823d6aa7233016e7b8b44a7eb57cc737c2aa90ef1`

**Reported observation:** Трасса 14.07.26: QENG servingcell LTE B7, QCAINFO PCC B7 и SCC B7/B3/B1 при NOCONN.

**Outcome limitation:** Реальные ответы от автора, активность транспорта данных на момент NOCONN не доказана.

**MedveModem implication (derived, not a statement by the author):** CA cell reports independent of IP session; do not infer attached data from SCC presence.

### zte-mf-family (1)

#### `4PDA-PRIMARY-MM-P15` — MM-P15 — ZTE MF833

**Classification:** `field-observation` · **risk:** `read` · **integration:** `additional-firmware-host-scoped-evidence` · **bench:** unverified

**Source:** [4PDA topic](https://4pda.to/forum/index.php?showtopic=968586&st=180) · 05.05.2020 · offline `ZTE_MF833__все_модели____обсуждение.html:printpost[196]` · source `ZTE_MF833__все_модели____обсуждение.html` · whole-source-HTML SHA256 `4ae4f93c4d7656c3ba35c6b1bae5f479b1b7c0cc3554b16dc6e460c4f45d01cb`

**Reported observation:** MF833R firmware did not accept ZCDRUN commands used by other ZTE variants.

**Outcome limitation:** First 501 printposts only; see primary HTML and research extract for reported result.

**MedveModem implication (derived, not a statement by the author):** No vendor-family blanket AT command permission.

## Offline test boundary

The 44 fixtures in `tests/fixtures/reference/4pda-field-regressions.json` are **scenarios for later integration / hardware validation**, NOT a claim that actual modem failures are reproduced by CI. `tests/test_4pda_field_regressions.py` validates safety, provenance and scenario contracts only.
