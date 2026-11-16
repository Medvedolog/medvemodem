# MedveModem — ubus API: русскоязычный путеводитель

[Полная спецификация API (English)](UBUS-API.md) · [Оглавление документации](README.md)

> **Назначение:** русская версия справочника. Технические идентификаторы, аргументы, имена методов, JSON-поля и значения перечислений оставлены как в исходном контракте. Для сверки используйте [английский оригинал](UBUS-API.md).

## Назначение

Это контракт между C-движком `medvemodemd` и пользовательскими интерфейсами (LuCI, CLI, внешним приложением MedveWRT). Источник состояния — объект `medvemodem` в системном ubus. Через rpcd LuCI вызывает его, например, с помощью `rpc.declare({ object: 'medvemodem', method: 'status' })`.

Документ сверялся с движком `0.15.0-r197` на 16.11.2026. Часть ответов проверена на стенде с эмуляцией ModemManager, но не каждое поле подтверждено на всех физических модемах. Отсутствующие поля и `null` — нормальное состояние «нет данных», а не ошибка интерфейса.

## Базовые правила

- **Обнаружение:** `status`. Ошибка ubus `NOT_FOUND` (код 4) означает, что объект не зарегистрирован (демон не запущен либо пакет не установлен). `PERMISSION_DENIED` (код 6) — ошибка прав rpcd ACL; эти случаи нужно различать.
- **Совместимость:** `status.version` сообщает версию движка, `device.schema` — версию формата устройства (сейчас 1). Новые поля можно добавлять; неизвестные поля клиент игнорирует, отсутствие известного трактует как неизвестное значение. Удаление/переименование поля требует увеличения `schema`.
- **Персональные идентификаторы:** IMEI, USB serial, IMSI, ICCID и номер SIM могут отображаться в интерфейсе, но их нельзя записывать в журнал. Диагностический экспорт скрывает эти данные.
- **Единственный владелец:** интерфейс не открывает AT/QMI-порты напрямую. Это делает движок либо выбранный им ModemManager. Сторонние `atinout` и `sms_tool` не должны опрашивать модем параллельно.
- **Опрос:** методы чтения обычно отвечают из памяти демона. `device` допустимо опрашивать каждые 2–5 секунд; радиоизмерения могут обновляться движком лишь раз в 30 секунд. `metrics` и `esim_status` не относятся к безопасному постоянному опросу: они могут обратиться непосредственно к модему/eUICC.

## Основные методы чтения

| Метод | Аргументы | Назначение |
|---|---|---|
| `status` | — | Версия и состояние демона; ModemManager, нагрузка и USB-классификация |
| `devices` | `absent` (необязательный bool) | Перечень модемов; можно включить отключённые |
| `device` | `id` | Подробная карточка устройства |
| `uplink` | — | Доступность Интернета, DNS, основной канал и резервы |
| `events` | `id`, необязательный `limit` | До 64 последних событий устройства, новые сначала |
| `recovery` | `id` | Состояние восстановления устройства |
| `lookup` | `node` | Какому модему принадлежит `ttyUSB*`, `cdc-wdm*` или интерфейс |
| `leases` | — | Аренда управляющих портов |
| `config_state` | `id` | Текущая конфигурационная транзакция |
| `sms_list` | `id`, `refresh`, `peek` | SMS из кэша; `peek` не помечает их прочитанными |
| `presets` | `id` | Безопасные AT-пресеты и USSD-коды оператора |
| `ussd_status` | `id` | Состояние текущего USSD-запроса |

Полные структуры ответов, поля `modemmanager`, `radio`, `connection`, `recovery` и статусы перечислены в [английском контракте](UBUS-API.md).

## Методы изменения состояния

Вызываются только при наличии соответствующих прав rpcd и проходят через арбитраж движка:

- `check`, `bringup`, `dial`, `hangup`, `connect` — проверка и управление соединениями.
- `recover`, `reregister`, `mm_restart`, `usb_rescue`, `usb_rescue_run` — ступенчатое восстановление. **Особенно опасно:** `mm_restart` затрагивает все модемы, а `usb_rescue_run` может переподключить весь USB-контроллер.
- `at_command` — AT-команда через проверку и аренду порта; опасные команды отклоняются.
- `config_plan`, `config_apply`, `config_rollback`, `config_release` — транзакционные изменения настроек.
- `priority`, `failover_manual`, `freeze`, `handover` — порядок каналов, ручной failover, заморозка и передача управления.
- `esim_action` — операции eSIM с учётом текущих ограничений данной версии.
- `sim_slot`, `fcc_unlock`, `fplmn`, `sms_clear`, `alerts`, `wlmode` — специализированные операции, в том числе изменяющие SIM или настройки модема.
- `cells`, `traffic_history`, `neighbours`, `usbtree`, `usbtree_detail`, `usb_class`, `web_open`, `web_do` — расширенные сведения и действия.

**Не вызывайте эти методы на основании одной только таблицы:** все аргументы, условия допустимости, побочные эффекты и формат ответов следует проверять по разделам [Write methods](UBUS-API.md#write-methods-later-stages-after-the-bench-confirms-them) и [Other methods](UBUS-API.md#other-methods-r163).

## Минимальные права на чтение (rpcd ACL)

```json
{
  "medvewrt-medvemodem-read": {
    "description": "Read MedveModem state",
    "read": {
      "ubus": {
        "medvemodem": [
          "status", "devices", "device", "uplink",
          "events", "recovery", "lookup"
        ]
      },
      "uci": ["medvemodem"]
    }
  }
}
```

ACL — шаблон для интерфейса только на чтение, не разрешающий запуск восстановления и AT-команд.

## Клиенты вне LuCI

Веб-сервер OpenWrt предоставляет JSON-RPC интерфейс `POST http(s)://<router>/ubus` через `uhttpd-mod-ubus`. Сначала выполняют `session.login`, получают `ubus_rpc_session`, затем вызывают `medvemodem.device` с действующим токеном. В ответе `result[0]` — код ubus (0 — успех, 4 — нет объекта, 6 — нет разрешения), `result[1]` — полезная нагрузка.

Токен и пароль хранить в защищённом хранилище устройства, не журналировать. Предпочтителен HTTPS с проверкой сертификата; при использовании HTTP пароль идёт открытым текстом. **Нельзя публиковать `/ubus` в WAN** — для внешнего доступа нужен VPN. В фоне опрос не чаще интервала, разрешённого системой (для Android WorkManager обычно не менее 15 минут); уведомления лучше передавать через Telegram-бот.

Полные JSON-примеры входа и вызовов: [Clients outside LuCI](UBUS-API.md#clients-outside-luci-an-android-app).

## Поля записи `device` — подробный справочник

| Карточка | Поля и смысл |
|---|---|
| Список / переключение | `device_id`, `present`, `product`, `manufacturer`, `modemmanager.model`; назначенное пользователем `name` из UCI `medvemodem.@modem[].name`; `name_follows` = `serial` / `imei` / `port` — за чем следуют имя и настройки при переподключении к другому USB-порту (HANDOFF 167). `connection.usb_config` содержит `count`, `current`, `wanted?`, `state` (`kept`/`set`/`waiting`/`refused`/`failed`), `why` для модемов с несколькими USB-композициями (HANDOFF 168). |
| Регистрация / оператор | `modemmanager.registration`: `home`, `roaming`, `searching`, `denied`, `idle`; `operator`, `operator_code` (PLMN), `access_tech` (например `lte`, `5gnr`), `location` с `mcc`, `mnc`, `tac`, `cid`. |
| Отказ регистрации (r183) | `connection.registration_denied` с `since`, `at`, `retry_in_s`. Если сеть отказала и новой регистрации не было, отметка остаётся даже при отключённом ModemManager модеме; повторная попытка движка через 30 минут. |
| SIM | `modemmanager.unlock_required` (`sim-pin`, `sim-puk` или `null`), `sim` (путь объекта либо `null`, когда карты нет), `sim_card` с `operator_code`, `operator_name`, `gid1`, `imsi`, `iccid`; `own_numbers`. Подпись пользователя `sim_label` (`number`, `description`) берётся из UCI `config sim` по `iccid`. |
| Уровень сигнала | `modemmanager.signal_quality` — процент; `signal.lte` (`rsrp`, `rsrq`, `rssi`, `snr`), `signal.5g` (`rsrp`, `rsrq`, `snr`), `umts` (`rscp`, `ecio`), `gsm` (`rssi`). По `--signal-get`, обновление раз в 30 секунд. |
| Настройки радиомодуля | `radio_settings`: `via` (`modemmanager`/`qmodem`/`at`/`none`), `dialect`, `modes.allowed` и `preferred`, `supported_modes`, `bands`, `supported_bands` (например `eutran-3`/`ngran-78`), `bands_any`, `age_s`, `writable`, `why`. Общая структура при любом исполнителе; показания QModem/AT запрашиваются методом `radio` по требованию. |
| Соты и антенны | `modemmanager.cells`: массив с `type` (`lte`/`5gnr`), `serving`, `role` (`primary`/`secondary`/`scg-primary`/`scg-secondary`/`neighbour`), `earfcn`/`nrarfcn`, `band`, `pci`, `rsrp`, `rsrq`, `sinr`, `bandwidth` (кГц из QMI), `tac`, `ci`, `plmn`. `current_bands` — рассчитанные полосы. `antennas`: цепочки с `chain`, `tuned`, `rsrp`, `power`, `ecio`, `phase`; передача `tx.traffic`/`power`; причина `why`. `radio_at` показывает сведения AT-опроса. |
| Устройство | `modemmanager.manufacturer`, `model`, `firmware`, `imei`; `vidpid`; `profile.id` для инженерного режима. |
| Исполнитель / владелец | `owner.control`, `owner.session`, `owner.at`: `modemmanager`/`qmodem`/`native`/`none`/`unresolved`, возможны профильные `vendor` и другие — неизвестное значение выводить без изменений. `profile.prefer_owner`, `status.modemmanager.backend`. |
| Автоподбор APN | `connection.apn_auto` = true при UCI `config device / option apn_auto '1'`. `connection.apn` — явно введённый APN, `apn_in_use` — APN интерфейса. Введённый адрес приоритетнее; если он пустой, движок пишет `connection.apn_suggestion` (APN, логин, тип авторизации `pap-or-chap` → `both`). При смене SIM после отключения сессии подбор повторяется; событие `apn-auto`. |
| Последний AT-дозвон | `connection.dial`: `t`, `result` (`connected`/`failed`), `steps`, `pdp?`, `reused?`, `error?`. Последняя попытка движка, например FM350 без QModem, только RAM; адреса не содержит. |
| Фиксация TTL | `connection.ttl`: `value`, `state` (`on`/`applying`/`waiting`/`no-egress`/`failed`), `device`. Только если задано UCI `config device / option ttl`. |
| Соединение | `session`: `state` (`connected`/`connecting`/`disconnected`/`failed`), `interface`, `proto`, `netifd` (`ipv4`, `uptime`). `uplink`: `device`, `role`, `reach` (`ok`, `target`, `latency_ms`, `age_s`), `traffic` (`rx_bytes`, `tx_bytes`). |
| Диагностика цепочки | `diagnosis`: `ok`, первый неисправный `link` (`device`/`network`/`session`/`internet`/`dns` или `null`), `reason`, `detail`, `standby`, `links` с состояниями `ok`/`fail`/`unknown`/`skipped`/`warn`, `warning` для DNS (`dns-intercepted`/`dns-stub`/`dns-rewritten`). |
| Проверка DNS | `uplink.dns_check`: `device`, `at`, `resolvers` (для каждого `kind`, `address`, `identity`, `udp_ms`, `tcp_ms`, `verdict`), `intercept` (`ok`/`intercepted`/`skipped`), `nxdomain` (`ok`/`rewritten`/`no-answer`/`error`/`skipped`), `names` с DoH/обычным DNS, `blocked` (`ok`/`stub`/`skipped`). |
| Надёжность | `stability`: `state` (`stable`/`flapping`/…), `physical_disconnects`, `usb_reenumerations`, `driver_rebinds`, `hourly[24]`. `health.state`; `recovery`: `state` (`idle`/`running`/`cooldown`/`manual_required`), `attempts_1h`, `last_success`, `last_failure`, `levels` (`name`, `supported`, `automatic`, `why`), `recent`. |
| Последнее событие | `last_event`: `t` (Unix time), `m` (монотонные мс), `kind`, `detail?`, `requested?`; отсутствует, если журнал пуст. Остальные события доступны через `events`, новые сначала. |

## Подробности методов и ограничений

### Подключение, приоритет и восстановление

- `check` — немедленно проверить доступность сети и DNS.
- `bringup` — инициировать поднятие соединения.
- `dial` / `hangup` — открыть/закрыть сессию (`id`, `apn`, `pdptype`). Ответ дозвона: `result` (`connected`/`failed`), `ipv4`, `prefix_len`, `gateway` (может отсутствовать — тогда маршрут по умолчанию на канале), `dns`, `error`. Для рецепта `flow=3gpp` также `pdp` (тип, реально получивший адрес), `reused` (повторно использована существующая сессия), `plmn`, `steps` (HANDOFF 171).
- `recover` — ступень восстановления с `id`, `level`, `reason`, `wait`.
- `at_command` — `id`, `command`, `timeout_ms`; движок отклоняет опасные команды и AT-вызовы, конфликтующие с ModemManager.
- `esim_action` — `id`, `action`: `version`/`probe`/`chip`/`profiles`; `enable` с `iccid` активирует уже установленный профиль eUICC. Ответ `{ok, restart_advised}`; ICCID не возвращается и не журналируется. Загрузка/удаление — не для Alpha 1.
- `config_plan`, `config_apply`, `config_rollback`, `config_release` — транзакционный план, применение, откат и снятие конфигурационной блокировки; `lease_*` — арбитраж аренды ресурсов.
- `rescan` / `probe` — пересканирование и проба.
- `mm_restart` (`confirm=true`, `reason`) — аварийно перезапустить ModemManager по требованию владельца, затронув **все модемы** роутера. Не чаще раза в пять минут; возвращает `started`/`running`/`refused`, причины `why` и `retry_s`. Остальные методы работы с модемом используют его `device_id`.

**`connect`** (`id`, `up` bool, необязательный `who`, r26) — кнопки «Отключить/Подключить» для netifd через движок с именем инициатора в журнале (`interface-down`/`interface-up`). Ответ `device_id`, `result` (`done`/`busy`/`refused`/`failed`), `interface`, `reason`. `busy` при занятом модеме (eSIM, восстановление, bringup, изменение конфигурации); `refused`, если интерфейса нет. netifd сохраняет выключенный автозапуск, и движок не поднимает такую сессию сам; LuCI больше не получает прямой up/down к `network.interface` в ACL.

**`priority`** (`order`: массив `device_id`, r33) — порядок модемов, первый считается главным. Метрики интерфейсов становятся 10, 20, 30…; модем без интерфейса всё равно занимает место. При изменениях netifd перезагружается один раз, каждому модему записывается событие `priority`. Ответ: `result`, `changed` (число изменённых метрик), `items` с `device_id`, `priority`, `interface`, `metric`, а также `reason`. `connection.metric` содержит последнюю считанную метрику.

### Регистрация, мягкий опрос и соты

**Cell info** (`-c`, UCI `core.cell_info`, r33) по умолчанию выключен: движок не вызывает ModemManager `--location-get`, `--location-enable-3gpp`, `--get-cell-info`. На стенде 15.10.2026 Orange Pi Zero 3 с T99W175 от этих запросов зависал в пределах минуты; если функция выключена, `modemmanager.cells`, положение и история несущих могут отсутствовать.

**Gentle poll** (`-g <ms>`, UCI `core.mm_poll_s` в секундах, r34) по умолчанию выключен. Отображение страницы/истории сигнала читает ModemManager не чаще указанного интервала; события добавления модема, bringup и recovery обходят ограничитель. При зависаниях драйвера от частого обновления страницы применяют `uci set medvemodem.core.mm_poll_s=60`.

**`cells`** (`id`, чтение, r34) — обслуживающие соты по AT при выключенном ModemManager cell info: Foxconn SDX55 T99W175 (`foxconn-sdx`), X20 T77W968/DW5821e (`foxconn-t77`), команда `AT^DEBUG?` через порт движка не чаще раза в 30 секунд. Ответ: `device_id`, `source` (`at`/`modemmanager`/`none`), `cells` с `type`, `serving`, `role`, `band`, `earfcn`/`nrarfcn`, `pci`, `bandwidth`, `rsrp`, `rsrq`, `sinr`, `rssi`, `tac`, `ci` (hex), `plmn`, `age_ms` при повторном использовании и `why`. Значения доступны в `modemmanager.cells` с `cells_source=at` две минуты; сырой ответ не сохраняется и не журналируется (он может содержать TMSI/IP). Соседние соты по AT пока не читаются.

### SMS, USSD и PIN/PUK

С r23–r24 SMS и USSD обслуживает ModemManager для принадлежащих ему модемов, а при его отсутствии/ошибке — AT-порт движка. `path`/`via` показывают `modemmanager` или `at`; `fallback`/`mm_error` — причину перехода на AT. При чтении по AT есть `store` (`name`, `used`, `total`) и `parts`, `have`/`total` для незавершённых составных SMS. На устройствах QModem или без пригодного порта может быть `refused`.

- `sms_send` (`id`, `number` формата `+?[0-9]{3,20}`, UTF-8 `text` до 1600 байт). Ответ: `ok`, `via?`, `parts?`, `error?` (не создано / создано, но не отправлено / тайм-аут / не отправлено / отправлено частично), `detail?` (ответ модема/MM). Неотправленный черновик MM удаляется.
- `sms_delete` (`id`, `sms`: идентификатор сообщения) → `{ok}`.
- `sms_center` (`id`, необязательный `number`) читает SMSC из SIM или предварительно записывает номер по `AT+CSCA` через собственный AT-порт движка. Ответ `ok`, `center?`, `written?`, `via=at`, `error?`, `detail?`; журнал `sms-center` (`written`/`failed`).
- `sms_storage` (`id`) переключает SMS-хранилище на SIM и обратно (`AT+CPMS="SM","SM","SM"`, затем `"ME","ME","ME"`), через порт движка, по полевому рецепту T99 после перепрошивки. Ответ `ok`, `storage?`, `error?`, `detail?`; журнал `sms-storage`.
- `ussd_start` (`id`, `code` формата `[0-9*#+]{2,32}`), `ussd_respond` (`id`, `reply`), `ussd_cancel` (`id`) сразу возвращают `result=started`; результат считывается `ussd_status`. Если в течение 60 секунд никто не отвечает на USSD-меню, движок отменяет сессию (`closed`).
- `sim_unlock` (`id`, `pin` 4–8 цифр, `puk?` ровно 8 цифр, `confirm?`; ACL на запись) разблокирует SIM через ModemManager (`mmcli -i <sim> --pin=` или `--puk= --pin=<new PIN>` без shell, по одной операции на модем, максимум 20 секунд). Ответ `ok`, `error?` с удалёнными последовательностями из 4+ цифр, `retries?` (`sim-pin`, `sim-puk`), `unlock_required?`; либо `refused`, если ввод некорректен, это не модем MM, SIM не заблокирована таким способом или осталась одна попытка PIN/PUK без `confirm=true`.

Журналы: `sms-received` (количество), `sms-sent` (успех/ошибка), `sms-stuck-dropped` (многочастное сообщение застряло в `receiving` дольше 300 секунд и блокирует приём), `sim-unlock` (успешно/неуспешно). **Номера, тексты, PIN/PUK, USSD-коды и ответы не журналируются и не возвращаются в ошибках.**

`modemmanager.unlock_retries` — остаток попыток. Сохранённый оператором PIN находится в секции устройства UCI как `option pin`, записывается в `option pincode` интерфейса netifd `modemmanager`/`qmi`/`mbim`; API выдаёт лишь `connection.pin_saved` и показывает `(hidden)` в планах, журналах и диагностике.

