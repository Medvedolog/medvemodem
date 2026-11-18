#!/bin/sh
# MedveModem SSH installer for OpenWrt 24.10 / 25.12.
# Interactive mode: sh /tmp/medvemodem-install.sh
set -eu

REPO=https://github.com/Medvedolog/medvemodem
TAG=${MEDVEMODEM_TAG:-v0.16.0-alpha1-r5}
ACTION=menu
BOT=ask
FORCE=0
PROXY=${https_proxy:-${HTTPS_PROXY:-}}
TMP=
trap '[ -z "$TMP" ] || rm -rf "$TMP"' EXIT

say() { printf '%s\n' "$*"; }
fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
usage() {
    cat <<'HELP'
Usage: sh medvemodem-install.sh [--install|--update|--reinstall|--remove|--remove-bot]
  [--tag v0.16.0-alpha1-r5] [--bot|--no-bot] [--proxy http://host:port] [--force]
--install      install or upgrade engine + LuCI
--update       update an existing installation
--reinstall    force reinstall of the same version (does not skip checks)
--remove       remove engine, LuCI and optionally bot
--remove-bot   remove bot and its LuCI only
No persistent proxy, feeds or network configuration is changed.
HELP
}
while [ "$#" -gt 0 ]; do
    case "$1" in
        --install) ACTION=install ;;
        --update) ACTION=update ;;
        --reinstall) ACTION=install; FORCE=1 ;;
        --remove) ACTION=remove ;;
        --remove-bot) ACTION=remove-bot ;;
        --force) FORCE=1 ;;
        --bot) BOT=yes ;;
        --no-bot) BOT=no ;;
        --tag|--proxy)
            opt=$1
            [ "$#" -gt 1 ] || fail "missing $opt value"
            if [ "$opt" = --tag ]; then TAG=$2; else PROXY=$2; fi
            shift ;;
        -h|--help) usage; exit 0 ;;
        *) fail "unknown argument $1" ;;
    esac
    shift
done
[ "$(id -u)" = 0 ] || fail 'run as root on the router'
[ -r /etc/openwrt_release ] || fail 'OpenWrt release file not found'
. /etc/openwrt_release
ARCH=${DISTRIB_ARCH:-}
REL=${DISTRIB_RELEASE:-}
case "$REL" in
    24.10.*) PM=opkg; EXT=ipk; SDK_RELEASE=24.10.8 ;;
    25.12.*) PM=apk; EXT=apk; SDK_RELEASE=25.12.6 ;;
    *) fail "unsupported OpenWrt $REL" ;;
esac
case "$ARCH" in aarch64_cortex-a53|aarch64_generic|x86_64) ;; *) fail "no release archive for $ARCH" ;; esac
case "$TAG" in v[0-9A-Za-z._-]*) ;; *) fail 'invalid tag' ;; esac
case "$TAG" in *[!A-Za-z0-9._-]*) fail 'invalid tag' ;; esac
command -v "$PM" >/dev/null 2>&1 || fail "$PM not installed"
command -v sha256sum >/dev/null 2>&1 || fail 'sha256sum required'
command -v tar >/dev/null 2>&1 || fail 'tar required'
command -v curl >/dev/null 2>&1 || command -v wget >/dev/null 2>&1 || fail 'curl or wget required'
TMP=$(mktemp -d /tmp/medvemodem-inst.XXXXXX) || fail 'cannot create temp dir'
chmod 700 "$TMP"
TTY=/dev/tty
[ -r "$TTY" ] || TTY=/dev/null
ask() {
    [ "$TTY" != /dev/null ] || { [ "$2" = yes ]; return; }
    printf '%s [%s] ' "$1" "$( [ "$2" = yes ] && echo Y/n || echo y/N )" > "$TTY"
    reply=
    read -r reply < "$TTY" || :
    case "$reply" in [Yy]*) return 0 ;; [Nn]*) return 1 ;; '') [ "$2" = yes ] ;; *) return 1 ;; esac
}
select_proxy() {
    if [ -z "$PROXY" ]; then
        [ "$TTY" != /dev/null ] || fail 'no proxy supplied and direct connection unavailable'
        printf 'HTTP mixed proxy URL (http://host:port): ' > "$TTY"
        read -r PROXY < "$TTY" || :
    fi
    case "$PROXY" in http://*|https://*) ;; *) fail 'expected HTTP(S) proxy URL' ;; esac
    case "$PROXY" in *' '*|*';'*) fail 'invalid proxy URL' ;; esac
    if [ "$TTY" != /dev/null ] && ask 'Does the proxy need login/password?' no; then
        printf 'Username: ' > "$TTY"
        read -r puser < "$TTY" || fail 'cannot read username'
        printf 'Password (hidden): ' > "$TTY"
        saved_tty=$(stty -g < "$TTY" 2>/dev/null || :)
        stty -echo < "$TTY" 2>/dev/null || :
        ppass=
        read -r ppass < "$TTY" || :
        [ -z "$saved_tty" ] || stty "$saved_tty" < "$TTY" 2>/dev/null || :
        printf '\n' > "$TTY"
        # URL metacharacters must be percent encoded in --proxy instead.
        case "$puser$ppass" in *'@'*|*':'*|*'/'*|*'%'*|*'?'*|*'#'*|*' '*|*'\\'*|*'&'*) fail 'percent-encode special characters in proxy URL' ;; esac
        scheme=${PROXY%%://*}
        host=${PROXY#*://}
        PROXY="$scheme://$puser:$ppass@$host"
        unset puser ppass
    fi
    export http_proxy="$PROXY" https_proxy="$PROXY" HTTP_PROXY="$PROXY" HTTPS_PROXY="$PROXY"
    say 'Temporary proxy activated (credentials will not be printed).'
}
no_proxy_env() {
    env -u http_proxy -u https_proxy -u HTTP_PROXY -u HTTPS_PROXY -u ALL_PROXY -u all_proxy "$@"
}
fetch() {
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --connect-timeout 8 --max-time 120 --retry 1 -o "$2" "$1"
    else
        wget -q -T 30 -O "$2" "$1"
    fi
}
fetch_direct() {
    if command -v curl >/dev/null 2>&1; then
        no_proxy_env curl -fsSL --connect-timeout 7 --max-time 35 -o "$2" "$1"
    else
        no_proxy_env wget -q -T 20 -O "$2" "$1"
    fi
}
installed() {
    if [ "$PM" = opkg ]; then
        opkg status "$1" 2>/dev/null | grep -q '^Status:.*installed'
    else
        apk info -e "$1" >/dev/null 2>&1
    fi
}
remove_packages() {
    selected=
    for name in "$@"; do
        if installed "$name"; then selected="$selected $name"; fi
    done
    [ -n "$selected" ] || { say 'Nothing to remove'; return 0; }
    say "Removing:$selected"
    if [ "$PM" = opkg ]; then opkg remove $selected; else apk del $selected; fi
}
remove_flow() {
    if [ "$ACTION" = remove-bot ]; then
        ask 'Remove bot and its LuCI? Settings are kept where supported.' no || return 0
        remove_packages luci-app-medvemodem-bot medvemodem-bot
    else
        ask 'Remove engine, LuCI and optional bot? QModem is not removed.' no || return 0
        remove_packages luci-app-medvemodem-bot medvemodem-bot luci-app-medvemodem medvemodem
    fi
    say 'Done. No explicit deletion of configuration or credentials performed.'
}
prepare_network() {
    manifest="$REPO/releases/download/$TAG/SHA256SUMS"
    say 'Checking GitHub directly...'
    if fetch_direct "$manifest" "$TMP/SHA256SUMS"; then
        say 'GitHub direct OK'
    else
        # HTTP 404 from a published release asset means "not found", not a
        # connectivity failure. Check the release API via a direct route first,
        # before prompting for a proxy that cannot fix a missing tag.
        if fetch_direct "https://api.github.com/repos/Medvedolog/medvemodem/releases/tags/$TAG" "$TMP/release-info"; then
            fail "GitHub is reachable, but SHA256SUMS is missing for $TAG. Check release assets."
        fi
        if fetch_direct "https://api.github.com" "$TMP/api-probe"; then
            fail "GitHub is reachable, but $TAG is not published (or not accessible). Select an existing --tag; a proxy cannot fix HTTP 404."
        fi
        say 'GitHub direct unavailable (network or API inaccessible)'
        select_proxy
        fetch "$manifest" "$TMP/SHA256SUMS" || fail 'GitHub release or SHA256SUMS missing/inaccessible via proxy'
    fi
    say 'Checking OpenWrt feeds directly...'
    if no_proxy_env "$PM" update; then
        say 'OpenWrt feeds direct OK'
    else
        say 'OpenWrt feeds direct unavailable'
        [ -n "${http_proxy:-}" ] || select_proxy
        "$PM" update || fail 'OpenWrt feeds unavailable through proxy; dependencies will not be forced'
    fi
}
choose_bot() {
    case "$BOT" in yes) return 0 ;; no) return 1 ;; esac
    ask 'Install/update Telegram bot and its LuCI too?' no
}
install_flow() {
    if [ "$ACTION" = update ] && ! installed medvemodem; then
        fail 'MedveModem is not installed; use --install'
    fi
    say "OpenWrt $REL / $ARCH / $PM; available release archive built for $SDK_RELEASE"
    if [ "$REL" != "$SDK_RELEASE" ]; then
        say "WARNING: firmware and package SDK patch version differ; dependency dry-run must succeed."
    fi
    prepare_network
    with_bot=0
    choose_bot && with_bot=1
    base=$(printf '%s' "$TAG" | sed 's/^v//')
    archive="medvemodem-$base-$ARCH-$SDK_RELEASE.tar.gz"
    say "Downloading $archive"
    fetch "$REPO/releases/download/$TAG/$archive" "$TMP/$archive" || fail 'release archive not found'
    expected=$(awk -v n="$archive" '$2 == n || $2 == "*"n {print $1; exit}' "$TMP/SHA256SUMS")
    [ -n "$expected" ] || fail 'SHA256 manifest does not list archive'
    actual=$(sha256sum "$TMP/$archive" | awk '{print $1}')
    [ "$expected" = "$actual" ] || fail 'SHA256 mismatch'
    say 'SHA256 verified'
    mkdir "$TMP/pkg"
    tar -tzf "$TMP/$archive" > "$TMP/members" || fail 'invalid release archive'
    while IFS= read -r member; do
        case "$member" in
            ./) ;;
            ./*) case "$(printf '%s' "$member" | cut -c3-)" in *'/'*|''|.*) fail 'unsafe archive member' ;; esac ;;
            *) fail 'unsafe archive member' ;;
        esac
    done < "$TMP/members"
    # Reject links and special file types before extracting as root.
    tar -tvzf "$TMP/$archive" > "$TMP/types" || fail 'archive metadata unavailable'
    if grep -Eq '^[^d-]' "$TMP/types"; then
        fail 'release archive contains links or special files'
    fi
    tar -xzf "$TMP/$archive" -C "$TMP/pkg" || fail 'archive extraction failed'
    files=
    package_files='medvemodem luci-app-medvemodem'
    [ "$with_bot" = 0 ] || package_files="$package_files medvemodem-bot luci-app-medvemodem-bot"
    for name in $package_files; do
        match=
        # Package filenames start with the exact name followed by a numeric
        # version. In particular, medvemodem-* would also match
        # medvemodem-bot-*.apk and falsely report a duplicate engine.
        for item in "$TMP"/pkg/"$name"_[0-9]*."$EXT" "$TMP"/pkg/"$name"-[0-9]*."$EXT"; do
            [ -f "$item" ] || continue
            [ -z "$match" ] || fail "duplicate $name package"
            match=$item
        done
        [ -n "$match" ] || fail "missing $name in release archive"
        files="$files $match"
    done
    say 'Checking package dependencies (dry-run)...'
    if [ "$PM" = opkg ]; then
        opkg install --noaction $files || fail 'missing/conflicting dependencies'
        if [ "$FORCE" = 1 ]; then
            opkg install --force-reinstall $files || fail 'reinstallation failed'
        else
            opkg install $files || fail 'installation failed'
        fi
    else
        apk add --simulate --allow-untrusted $files || fail 'missing/conflicting dependencies'
        if [ "$FORCE" = 1 ]; then
            apk add --force-reinstall --allow-untrusted $files || fail 'reinstallation failed'
        else
            apk add --allow-untrusted $files || fail 'installation failed'
        fi
    fi
    for name in $package_files; do installed "$name" || fail "missing installed $name"; done
    say 'Installed successfully. QModem is managed separately in LuCI.'
}
case "$ACTION" in
    menu)
        [ "$TTY" != /dev/null ] || fail 'for noninteractive use, supply an action flag'
        say '1) Install/update MedveModem + LuCI'
        say '2) Force reinstall same version'
        say '3) Remove MedveModem + LuCI (and bot)'
        say '4) Remove only bot and its LuCI'
        printf 'Choose [1-4]: ' > "$TTY"
        choice=
        read -r choice < "$TTY" || :
        case "$choice" in
            1) ACTION=install ;;
            2) ACTION=install; FORCE=1 ;;
            3) ACTION=remove ;;
            4) ACTION=remove-bot ;;
            *) fail 'cancelled' ;;
        esac ;;
esac
case "$ACTION" in
    install|update) install_flow ;;
    remove|remove-bot) remove_flow ;;
    *) fail 'invalid action' ;;
esac