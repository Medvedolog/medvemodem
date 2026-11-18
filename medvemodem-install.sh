#!/bin/sh
# MedveModem SSH installer for OpenWrt 24.10 / 25.12.
# Interactive mode: sh /tmp/medvemodem-install.sh
set -eu

REPO=https://github.com/Medvedolog/medvemodem
TAG=${MEDVEMODEM_TAG:-v0.16.0-alpha1-r6}
ACTION=menu
BOT=ask
FORCE=0
ALLOW_DISCONNECT=0
SERVICE_RESTORE=
SERVICES_STOPPED=0
PROXY=${https_proxy:-${HTTPS_PROXY:-}}
TMP=
# Lock the entire SSH installer session, and serialize package-manager writes
# with LuCI's pkg-install/self-update. Locks are kept under /tmp (OpenWrt).
INSTALL_LOCK=${MEDVEMODEM_INSTALL_LOCK:-/tmp/medvemodem/ssh-installer.lock}
PM_LOCK=${MEDVEMODEM_PM_LOCK:-/tmp/medvemodem/package-operation.lock}
INSTALL_HELD=0
PM_HELD=0
trap 'installer_exit' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# mkdir is the lock acquisition operation. Never delete a lock held by a
# living process, including the interval between mkdir and writing owner.
take_lock() {
    lock_dir=$1
    mkdir -p "${lock_dir%/*}"
    if mkdir "$lock_dir" 2>/dev/null; then
        echo $ > "$lock_dir/owner"
        return 0
    fi
    owner=$(cat "$lock_dir/owner" 2>/dev/null || :)
    if [ -n "$owner" ]; then
        case "$owner" in *[!0-9]*|'') return 1 ;; esac
        kill -0 "$owner" 2>/dev/null && return 1
    else
        # Another process might still be writing its PID.
        [ -n "$(find "$lock_dir" -maxdepth 0 -mmin +1 2>/dev/null)" ] || return 1
    fi
    mv "$lock_dir" "$lock_dir.stale.$" 2>/dev/null || return 1
    rm -rf "$lock_dir.stale.$"
    mkdir "$lock_dir" 2>/dev/null || return 1
    echo $ > "$lock_dir/owner"
}
release_lock() {
    [ "$(cat "$1/owner" 2>/dev/null || :)" = "$" ] && rm -rf "$1"
    return 0
}
take_package_lock() {
    take_lock "$PM_LOCK" || fail 'another MedveModem package operation is running'
    PM_HELD=1
}
say() { printf '%s\n' "$*"; }
fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
usage() {
    cat <<'HELP'
Usage: sh medvemodem-install.sh [--install|--update|--reinstall|--remove|--remove-bot]
  [--tag v0.16.0-alpha1-r6] [--bot|--no-bot] [--proxy http://host:port] [--force]
  [--allow-disconnect]
--install      install or upgrade engine + LuCI
--update       update an existing installation
--reinstall    force reinstall of the same version (does not skip checks)
--remove       remove engine, LuCI and optionally bot
--remove-bot   remove bot and its LuCI only
--allow-disconnect  permit service stop that may interrupt SSH and mobile internet
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
        --allow-disconnect) ALLOW_DISCONNECT=1 ;;
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
take_lock "$INSTALL_LOCK" || fail 'another SSH installer is already running'
INSTALL_HELD=1
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
# Preflight audit: conflicting modem owners/hotplug actors are not removed by
# this installer. Read-only checks; QModem/ModemManager are supported owners.
# The names below are signatures, not a blanket ban on SMS/diagnostic tools.
conflicts=0
conflict_line() {
    conflicts=$((conflicts + 1))
    say "CONFLICT: $1"
    say "  $2"
}
installed_any() {
    for candidate in "$@"; do
        if installed "$candidate"; then echo "$candidate"; fi
    done
}
audit_modem_owners() {
    conflicts=0
    say 'Checking existing modem managers and hotplug automation...'
    for name in luci-app-5gmodem 5gmodem luci-app-automodem automodem rooter luci-app-rooter modemmanager-auto modem-auto; do
        if installed "$name"; then
            if [ "$PM" = apk ]; then remove_cmd="apk del $name"; else remove_cmd="opkg remove $name"; fi
            conflict_line "installed package $name" "Review and remove manually if it controls the same modem: $remove_cmd"
        fi
    done
    # ROOter may be baked into squashfs without a package database entry.
    for marker in /usr/lib/rooter /etc/config/modem /etc/config/modemmanager-auto; do
        [ -e "$marker" ] || continue
        conflict_line "modem automation marker $marker" 'Inspect firmware-owned services and hotplug handlers; do not delete firmware files blindly.'
    done
    # Santa/RR custom hotplug deployments are often not installed as packages.
    # Inspect only dedicated name signatures; generic usbmode/netifd hooks stay.
    for dir in /etc/hotplug.d/usb /etc/hotplug.d/tty /etc/hotplug.d/net /etc/hotplug.d/iface; do
        [ -d "$dir" ] || continue
        for file in "$dir"/*; do
            [ -f "$file" ] || continue
            case "${file##*/}" in
                *santa*|*Santa*|*automodem*|*rooter*|*ROOter*|*5gmodem*|*qmi-autoconnect*|*modem-auto*)
                    conflict_line "custom hotplug handler $file" 'Inspect, disable, or remove its owning package/script manually before installing.'
                    ;;
            esac
        done
    done
    # Name the compatible controllers and passive Polish packages separately.
    for name in qmodem luci-app-qmodem luci-app-qmodem-next modemmanager; do
        installed "$name" && say "COMPATIBLE: $name (MedveModem can coordinate this owner; keep installed)"
    done
    for name in luci-app-3ginfo-lite 3ginfo-lite luci-app-modemdata modemdata \
                luci-app-modemband modemband luci-app-sms-tool-js \
                luci-app-sms-manager sms-tool; do
        installed "$name" && say "NOTICE: $name installed (diagnostic/control utility; review AT/cron polling, not an automatic blocker)"
    done
    if [ "$conflicts" -gt 0 ]; then
        say "Found $conflicts potential competing modem controllers/hotplug handlers."
        say "No packages, services or hotplug files were changed."
        say "Back up configuration and remove/disable conflicts manually, then rerun this installer."
        fail 'installation paused due to competing modem automation'
    fi
    say 'Modem ownership preflight: no named conflicts detected.'
}
remove_packages() {
    selected=
    for name in "$@"; do
        if installed "$name"; then selected="$selected $name"; fi
    done
    [ -n "$selected" ] || { say 'Nothing to remove'; return 0; }
    take_package_lock
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
show_release() {
    say ''
    say '============================================================'
    say "Available release: $TAG (MedveModem + LuCI)"
    say "OpenWrt: $REL ($PM), package arch: $ARCH"
    say "Package archive SDK: $SDK_RELEASE"
    say "Release page: $REPO/releases/tag/$TAG"
    say '------------------------------------------------------------'
    say 'RELEASE HIGHLIGHTS (English)'
    # Prefer a release-owned file; older releases might only contain a
    # GitHub release description. r5 has a standalone English summary.
    if fetch "$REPO/releases/download/$TAG/HIGHLIGHTS.txt" "$TMP/highlights" >/dev/null 2>&1 && [ -s "$TMP/highlights" ]; then
        cat "$TMP/highlights"
    elif [ "$TAG" = 'v0.16.0-alpha1-r6' ] &&
         fetch "https://raw.githubusercontent.com/Medvedolog/medvemodem/release/r6/docs/RELEASE-HIGHLIGHTS-ALPHA1-R6.md" "$TMP/highlights" >/dev/null 2>&1 &&
         [ -s "$TMP/highlights" ]; then
        cat "$TMP/highlights"
    elif command -v jsonfilter >/dev/null 2>&1 &&
         fetch "https://api.github.com/repos/Medvedolog/medvemodem/releases/tags/$TAG" "$TMP/release.json" >/dev/null 2>&1; then
        jsonfilter -i "$TMP/release.json" -e '@.body' 2>/dev/null || say 'No release highlights provided.'
    else
        say 'No release highlights provided.'
    fi
    say '============================================================'
    say ''
}
# Upgrade transaction guard: package post-upgrade hooks may start extra procd
# instances. Stop the previously active services first, then start them once.
# QModem may be this router's only WAN path: never stop it without consent.
svc_running() {
    [ -x "/etc/init.d/$1" ] || return 1
    # OpenWrt procd's "running" subcommand is more reliable than parsing
    # distro-specific status text; fall back to status for older init scripts.
    /etc/init.d/"$1" running >/dev/null 2>&1 ||
        /etc/init.d/"$1" status 2>/dev/null | grep -Eq '(^|[[:space:]])running([[:space:]]|$)'
}
stop_one() {
    service=$1
    [ -x "/etc/init.d/$service" ] || return 0
    say "Stopping $service..."
    if command -v timeout >/dev/null 2>&1; then
        timeout -s TERM -k 2 18 "/etc/init.d/$service" stop || say "WARNING: stop of $service timed out or failed"
    else
        "/etc/init.d/$service" stop || say "WARNING: stop of $service failed"
    fi
}
# Match processes by their exact service executable/script, never kill generic
# ucode, curl, sh or all modems' control programs.
service_pids() {
    case "$1" in
        medvemodemd) pidof medvemodemd 2>/dev/null || : ;;
        medvemodem-bot) ps w | awk '/[u]code \/usr\/share\/medvemodem-bot\/main[.]uc/ { print $1 }' ;;
        bot-poll) ps w | awk '/[c]url/ && /\/tmp\/medvemodem-bot\/curl-poll-[^ ]+[.]cfg/ { print $1 }' ;;
        qmodem) ps w | awk '/\/[u]sr\/share\/qmodem\/modem_dial[.]sh/ { print $1 }' ;;
    esac
}
wait_pids_gone() {
    family=$1
    n=0
    while [ "$n" -lt 10 ]; do
        pids=$(service_pids "$family")
        [ -z "$pids" ] && return 0
        sleep 1
        n=$((n + 1))
    done
    say "WARNING: $family has leftover PIDs: $pids"
    # First TERM, then KILL only exact identified family PIDs that survived.
    for pid in $pids; do
        case "$pid" in *[!0-9]*|'') continue ;; esac
        kill -TERM "$pid" 2>/dev/null || :
    done
    sleep 2
    pids=$(service_pids "$family")
    [ -z "$pids" ] && return 0
    say "WARNING: force-stopping leftover $family PIDs: $pids"
    for pid in $pids; do
        case "$pid" in *[!0-9]*|'') continue ;; esac
        kill -KILL "$pid" 2>/dev/null || :
    done
    sleep 1
    [ -z "$(service_pids "$family")" ] || fail "cannot clear old $family instances; refusing package upgrade"
}
restore_services() {
    [ "$SERVICES_STOPPED" = 1 ] || return 0
    SERVICES_STOPPED=0
    # Run QModem first, then its optional monitors; engine and bot last.
    for service in qmodem_network qmodem_monitor qmodem_hc_sim qmodem_init medvemodemd medvemodem-bot; do
        case " $SERVICE_RESTORE " in
            *" $service "*) if [ -x "/etc/init.d/$service" ]; then
                say "Restoring $service..."
                /etc/init.d/"$service" start || say "WARNING: $service did not restart"
            fi ;;
        esac
    done
}
installer_exit() {
    code=$?
    trap - EXIT
    restore_services || :
    if [ "$PM_HELD" = 1 ]; then release_lock "$PM_LOCK"; PM_HELD=0; fi
    if [ "$INSTALL_HELD" = 1 ]; then release_lock "$INSTALL_LOCK"; INSTALL_HELD=0; fi
    [ -z "$TMP" ] || rm -rf "$TMP"
    exit "$code"
}
quiesce_for_upgrade() {
    # An SSH session routed over QModem can die here. Require opt-in even
    # for --force/--reinstall: correctness is more important than uptime.
    if [ "$ALLOW_DISCONNECT" != 1 ]; then
        if [ "$TTY" = /dev/null ] || ! ask 'Upgrading stops MedveModem, QModem connections and Telegram bot temporarily. SSH/mobile internet may disconnect. Continue?' no; then
            fail 'upgrade cancelled; use --allow-disconnect only with a backup management path'
        fi
    fi
    SERVICE_RESTORE=
    for service in medvemodem-bot medvemodemd qmodem_monitor qmodem_hc_sim qmodem_init qmodem_network; do
        if svc_running "$service"; then SERVICE_RESTORE="$SERVICE_RESTORE $service"; fi
    done
    SERVICES_STOPPED=1
    for service in medvemodem-bot medvemodemd qmodem_monitor qmodem_hc_sim qmodem_init qmodem_network; do
        stop_one "$service"
    done
    wait_pids_gone medvemodemd
    wait_pids_gone medvemodem-bot
    wait_pids_gone bot-poll
    wait_pids_gone qmodem
    say 'Service stop and old-process checks completed.'
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
    take_package_lock
    audit_modem_owners
    prepare_network
    show_release
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
        if [ "$TTY" != /dev/null ]; then
            ask "Install $TAG packages now? ($package_files)" no || { say 'Cancelled, no packages changed.'; return 0; }
        fi
        quiesce_for_upgrade
        if [ "$FORCE" = 1 ]; then
            opkg install --force-reinstall $files || fail 'reinstallation failed'
        else
            opkg install $files || fail 'installation failed'
        fi
    else
        apk add --simulate --allow-untrusted $files || fail 'missing/conflicting dependencies'
        if [ "$TTY" != /dev/null ]; then
            ask "Install $TAG packages now? ($package_files)" no || { say 'Cancelled, no packages changed.'; return 0; }
        fi
        quiesce_for_upgrade
        if [ "$FORCE" = 1 ]; then
            apk add --force-reinstall --allow-untrusted $files || fail 'reinstallation failed'
        else
            apk add --allow-untrusted $files || fail 'installation failed'
        fi
    fi
    restore_services
    # A running service must not have inherited duplicated old processes.
    for family in medvemodemd medvemodem-bot; do
        count=0
        for _pid in $(service_pids "$family"); do count=$((count + 1)); done
        if [ "$count" -gt 1 ]; then
            say "WARNING: $family has $count processes after upgrade; inspect procd and process parents before killing anything."
        fi
    done
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