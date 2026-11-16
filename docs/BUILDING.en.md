# Building MedveModem from source

[Русский оригинал](BUILDING.md) · [Documentation index](README.md)

Use the published packages from [Releases](https://github.com/Medvedolog/medvemodem/releases) when available. Build from source to verify changes or target an architecture without a published build.

## Prerequisites

- Linux or WSL2 with Docker.
- Approximately 10 GB of free disk space for the OpenWrt SDK image and downloaded dependencies.
- Network access to OpenWrt package feeds and upstream lpac source.

## Build with one command

From the repository root:

```sh
tools/build.sh aarch64_cortex-a53 25.12.6
```

Argument 1 is the **OpenWrt package architecture** (e.g. `aarch64_cortex-a53`, `aarch64_generic`, `x86_64`); argument 2 is the OpenWrt release. The script runs `openwrt/sdk:<architecture>-<release>` in Docker, adds this repository as a local feed and builds the packages to `dist/<architecture>-<release>/`.

| Package | Purpose |
|---|---|
| `medvemodem` | Core daemon, CLI and helper scripts |
| `luci-app-medvemodem` | LuCI user interface |
| `medvemodem-bot`, `luci-app-medvemodem-bot` | Optional Telegram bot and LuCI pages |
| `medvemodem-lpac`, `medvemodem-lpac-uqmi` | Optional eSIM support |

OpenWrt 25.12 uses `.apk`; OpenWrt 24.10 uses `.ipk`. To limit parallelism, set `JOBS=4` before the command. The first run typically spends substantial time fetching and building dependencies.

## Using an existing SDK

```sh
echo "src-link medvemodem /path/to/parent-directory-of-medvemodem" >> feeds.conf
./scripts/feeds update -a
./scripts/feeds install -a -p medvemodem
make menuconfig
make package/medvemodem/compile package/luci-app-medvemodem/compile V=s
```

The feed path must point to the directory **above** the checkout because the repository-root `Makefile` describes the engine package; other packages are located in `packages/`.

If `git.openwrt.org` is unreachable, set `FEEDS_GITHUB=1` for `tools/build.sh` to use the upstream GitHub mirrors.

## Static libraries and compatibility

The engine statically embeds libubox, libubus and libuci (`tools/static-libs.sh` and `MEDVEMODEM_STATIC` in the root Makefile). This avoids runtime ABI dependencies on these three libraries, but **does not guarantee that one binary is compatible with every firmware snapshot**. Verify the exact release, remaining dependencies and package architecture.

## Installing the result

Follow [INSTALL.en.md](INSTALL.en.md). Install matching versions of the engine and its LuCI package together. The LuCI post-install script restarts rpcd, which may end your browser session.
