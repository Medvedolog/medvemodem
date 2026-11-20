# MedveModem Bot

**Зависимости `apk`/`opkg`:** `medvemodem` (не ниже версии бота), `ucode`, `ucode-mod-ubus`, `ucode-mod-uloop`, `ucode-mod-fs`, `ucode-mod-uci`, `ucode-mod-log`, `ucode-mod-digest`, `curl`, `ca-bundle`, `jq`. Для веб-страниц бота дополнительно `luci-app-medvemodem-bot` (зависит от `luci-base`, `luci-app-medvemodem`, `medvemodem-bot`). Всё ставится через `apk` или `opkg` из feeds и архива MedveModem; [вся матрица зависимостей](../../docs/DEPENDENCIES.md).



Telegram-бот для роутера с [MedveModem](../../README.md): оповещения о модемах и каналах, сводка, входящие SMS и
журнал в личном чате администратора. Отдельный пакет `medvemodem-bot` (и страница LuCI `luci-app-medvemodem-bot`);
модемами он не управляет сам — всё спрашивает у движка MedveModem через ubus и делает только то, что движок
разрешает. Написан на ucode, работает на OpenWrt 24.10 и 25.12, ставится на любую архитектуру (`noarch`/`all`).

Язык — русский или английский (`option lang`: `auto`, `ru`, `en`).

## Что умеет

**Оповещения**
- Сеанс оборвался и восстановлен, долгий сбой и «восстановлено», частые короткие сбои — одним сообщением.
- Смена основного канала, «белые списки» оператора, лимит трафика SIM, перезапуски ModemManager и QModem,
  сбои ядра роутера, недоступность проверяемых сервисов.
- Тихие часы (по умолчанию 23:00–07:00; срочное приходит всё равно); короткие сбои сеанса можно не получать
  (режим «умный», по умолчанию) или получать все.

**Экран и кнопки**
- Сводка с кнопками: модемы, каналы, лимиты трафика, журнал, входящие SMS, сеть, настройки. Одно сообщение
  обновляется на месте, «🔄 Обновить» — по нажатию.
- Карточка модема: оператор, сеть, сигнал, адрес, трафик, состояние звеньев, время без интернета за день.
- «За сутки»: сбои, переключения, перезапуски служб, трафик.
- «🌍 Сеть»: внешние адреса каналов, туннель, проверка сервисов.
- «🚧 Белые списки» (r185): каждый канал — открыт или только белый список; режим белых списков с переключателем;
  **🚦 тест блокировок** — по каждому модему, где сейчас белые списки, где нет (без переключения маршрутов роутера).
- Журнал событий; журнал модемов и весь журнал роутера — файлом (ссылки и токены в нём скрыты).

**SMS**
- Входящие SMS с отправителем и модемом, кнопка «Код» для цифр из сообщения. Бот не помечает SMS прочитанными.
- Тексты SMS показываются, только если это разрешено (`allow_sms_content`); пересылка — `sms_forward`.
- С `allow_send '1'` (r184): отправка SMS (`/send`, «Написать SMS» под входящими, «Ответить» под пересланным) —
  модем, номер или контакт, текст, затем страница с модемом, номером и текстом и кнопка «Отправить», одна отправка;
  USSD-запросы (`/ussd`, коды оператора кнопками, ответ на меню сообщением). Номера и тексты в журнал не пишутся.

**Управление (по желанию)**
- С `allow_control '1'`: подключить/отключить, переподключить, перерегистрировать, перезагрузить, сделать основным,
  приостановить/возобновить управление и менять исключения автопереключения. Отключение, переподключение,
  перерегистрация, перезагрузка, назначение первым и пауза управления требуют подтверждения; подключение и
  возобновление управления выполняются без отдельного подтверждения. Без `allow_control` бот только читает.
- Действия администратора пишутся в системный журнал (`admin_*`).

**Путь до Telegram**
- Напрямую, через прокси (SOCKS/HTTP, в том числе podkop и forkop, с логином) или через выбранный модем; при
  блокировке переключается на другой путь и возвращается на основной, когда тот снова работает.
- Находит прокси менеджеров на роутере сам и подсказывает адрес, если указанный не отвечает.

## Безопасность

- Отвечает только привязанному администратору в личном чате; чужие сообщения и группы — тишина.
- Токен хранится в отдельном файле с правами `0600`; в журналах, файлах состояния и командной строке curl его нет.
- IMEI, ICCID, IMSI, номера и тексты SMS не пишутся ни в журнал, ни на флеш.
- Работает от root, как службы самого OpenWrt; ubusd не перезапускает и своего пользователя не создаёт.

## Установка

Архив `medvemodem-bot-<версия>-<линия>.tar.gz` — на странице стендовой сборки
([Releases](https://github.com/Medvedolog/medvemodem/releases)). Нужен MedveModem той же версии.

```sh
# OpenWrt 25.12 (apk)
tar xzf medvemodem-bot-*-25.12.tar.gz && apk add --allow-untrusted ./medvemodem-bot-*.apk ./luci-app-medvemodem-bot-*.apk

# OpenWrt 24.10 (opkg)
tar xzf medvemodem-bot-*-24.10.tar.gz && opkg install ./medvemodem-bot_*.ipk ./luci-app-medvemodem-bot_*.ipk
```

Зависимости ставятся сами: `ucode` с модулями ubus, uloop, fs, uci, log и digest, `curl`, `ca-bundle`, `jq`.

## Первый запуск

1. Создайте бота у [@BotFather](https://t.me/BotFather) и скопируйте токен.
2. LuCI: «Модем» → «MedveModem» → «Telegram-бот». Мастер настройки: токен → проверка пути до Telegram → числовой
   Telegram user ID администратора (его подскажет @userinfobot) → «Включить и запустить бота».
3. Проверка — «Отправить тестовое сообщение»; в Telegram — `/menu`.

Команды: `/menu`, `/status`, `/modems`, `/channels`, `/sms`, `/send`, `/ussd`, `/network`, `/quota`, `/events`, `/log`, `/logall`,
`/settings`, `/help` — в меню «☰» у поля ввода, с подсказками; постоянных кнопок под полем ввода нет.

Настройки — `/etc/config/medvemodem-bot` (через LuCI или «⚙️ Настройки» в самом боте, если `allow_settings '1'`);
служба — `/etc/init.d/medvemodem-bot`.

## Если что-то не так

- Статус бота и путей до Telegram — на странице «Telegram-бот» в LuCI («Обзор»).
- `logread -e medvemodem-bot` — коды состояния без личных данных.
- Отчёт — «Скачать журнал» в боте или «Диагностика» → «Отчёт о модеме» в LuCI.

Подробности: [архитектура бота и макеты Telegram-карточек](../../docs/BOT-ARCHITECTURE.md), ТЗ бота, заметки к сборкам,
[история изменений](../../docs/CHANGELOG.md).

---

## In English

MedveModem Bot is the Telegram side of [MedveModem](../../README.md): alerts about modems and channels, a summary,
incoming SMS and the journal in the administrator's private chat. A separate package (`medvemodem-bot`, LuCI page
`luci-app-medvemodem-bot`) written in ucode for OpenWrt 24.10 and 25.12; it asks the MedveModem engine over ubus for
everything and changes nothing the engine does not allow. Alerts with quiet hours; a modem card; a daily summary;
external addresses, service checks and a block test per modem; the journal and the router's log as files; incoming
SMS (content only when allowed); optional control with confirmations (`allow_control`). It reaches Telegram
directly, through a proxy (podkop and forkop included) or through a chosen modem, and switches paths when one is
blocked. Only the bound administrator gets answers; the token, SIM identifiers and SMS text are never logged. Russian
or English (`option lang`). Install the `medvemodem-bot` archive of the same bench build as MedveModem, then set it up
in LuCI: «Modem» → «MedveModem» → «Telegram bot».
