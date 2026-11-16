# Сборка из исходников

Готовые архивы лежат на странице Releases. Собирать самому нужно, чтобы проверить изменение, или если нужна
архитектура, которой там нет.

## Что нужно

- Linux или WSL2 с Docker.
- Около 10 ГБ на диске под образ OpenWrt SDK и исходники зависимостей.
- Сеть: SDK скачивает фиды OpenWrt и исходники lpac.

## Одной командой

Из корня репозитория:

```sh
tools/build.sh aarch64_cortex-a53 25.12.6
```

Первый аргумент — архитектура пакетов OpenWrt (`aarch64_cortex-a53`, `aarch64_generic`, `x86_64`, …), второй —
версия OpenWrt. Скрипт запускает официальный образ `openwrt/sdk:<архитектура>-<версия>`, подключает этот репозиторий
как фид и собирает пакеты. Готовые пакеты появятся в `dist/<архитектура>-<версия>/`:

| Пакет | Что это |
|---|---|
| `medvemodem` | движок, консоль и служебные скрипты |
| `luci-app-medvemodem` | страницы LuCI |
| `medvemodem-bot`, `luci-app-medvemodem-bot` | Telegram-бот и его страницы (необязательно) |
| `medvemodem-lpac`, `medvemodem-lpac-uqmi` | eSIM (необязательно) |

Для OpenWrt 25.12 и снапшота получаются `.apk`, для 24.10 — `.ipk`. Число потоков сборки задаёт `JOBS=4
tools/build.sh …`. Первая сборка идёт долго, около 30–40 минут: SDK собирает зависимости.

## Вручную в своём SDK

Если SDK уже стоит:

```sh
echo "src-link medvemodem /путь/к/папке-над-medvemodem" >> feeds.conf
./scripts/feeds update -a
./scripts/feeds install -a -p medvemodem
make menuconfig        # Network → WWAN → medvemodem, LuCI → 3. Applications → luci-app-medvemodem
make package/medvemodem/compile package/luci-app-medvemodem/compile V=s
```

Фид — папка *над* клоном репозитория: корневой `Makefile` сам пакет (движок), а остальные лежат в `packages/`.

Если git.openwrt.org не отвечает, `FEEDS_GITHUB=1 tools/build.sh …` берёт фиды OpenWrt с их зеркал на GitHub.

## Зачем библиотеки вшиты

libubox, libubus и libuci вшиваются в движок статически (`tools/static-libs.sh`, `MEDVEMODEM_STATIC` в `Makefile`).
Так один архив подходит ко всем 25.12.x и снапшоту, а другой — ко всем 24.10.x: у снапшота другие версии этих
библиотек, и пакет со ссылкой на них не ставился бы.

## Установка собранного

Как в [INSTALL.md](INSTALL.md): движок и LuCI ставятся парой одной версии, после установки — `/etc/init.d/rpcd restart`.
