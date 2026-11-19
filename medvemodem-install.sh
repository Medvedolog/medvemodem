#!/bin/sh
# MedveModem SSH installer for OpenWrt 24.10 / 25.12.
# Interactive mode: sh /tmp/medvemodem-install.sh
set -eu

REPO=https://github.com/Medvedolog/medvemodem
TAG=${MEDVEMODEM_TAG:-}
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
        echo $$ > "$lock_dir/owner"
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
    mv "$lock_dir" "$lock_dir.stale.$$" 2>/dev/null || return 1
    rm -rf "$lock_dir.stale.$$"
    mkdir "$lock_dir" 2>/dev/null || return 1
    echo $$ > "$lock_dir/owner"
}
release_lock() {
    [ "$(cat "$1/owner" 2>/dev/null || :)" = "$$" ] && rm -rf "$1"
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
  [--tag v0.16.0-alpha1-r8] [--bot|--no-bot] [--proxy http://host:port] [--force]
  [--allow-disconnect]
--install      install or upgrade engine + LuCI
--update       update an existing installation
--reinstall    force reinstall of the same version (does not skip checks)
--remove       remove engine, LuCI and optionally bot
--remove-bot   remove bot and its LuCI only
--allow-disconnect  permit service stop that may interrupt SSH and mobile internet
Default: newest published MedveModem release (stable OR prerelease), not GitHub latest (stable only).
--tag overrides automatic selection with an exact version.
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
if [ -n "$TAG" ]; then
    case "$TAG" in v[0-9]*) ;; *) fail 'invalid tag: expected v<version>' ;; esac
    case "$TAG" in *[!A-Za-z0-9._-]*) fail 'invalid tag' ;; esac
fi
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
installed_version() {
    if [ "$PM" = opkg ]; then
        opkg status "$1" 2>/dev/null | sed -n 's/^Version: //p' | head -n 1
    else
        awk -v name="$1" '
            /^P:/ { pkg = substr($0, 3) }
            /^V:/ && pkg == name { print substr($0, 3); exit }
        ' /lib/apk/db/installed 2>/dev/null
    fi
}
command -v jsonfilter >/dev/null 2>&1 || fail 'jsonfilter required for GitHub release selection'
# Use the SAME detector as LuCI (files/usr/libexec/medvemodem/neighbours).
# On a clean router that file is not installed yet. Fetch the canonical script
# from an immutable repository commit rather than maintaining a second catalog.
# This is a read-only audit: only explicit controller evidence blocks install.
NEIGHBOURS_COMMIT=4bd47a3a5d48234eb0a17675bbb1810e3fd1ac0d
audit_modem_owners() {
    local level kind name evidence conflicts=0 five_count=0 five_more=0 five_list=
    say 'Checking existing modem controllers using MedveModem neighbours detector...'
    detector="https://raw.githubusercontent.com/Medvedolog/medvemodem/$NEIGHBOURS_COMMIT/files/usr/libexec/medvemodem/neighbours"
    fetch "$detector" "$TMP/neighbours" ||
        fail 'cannot download the shared modem audit; refusing to install without preflight'
    sh "$TMP/neighbours" --audit > "$TMP/neighbours.audit" ||
        fail 'shared modem audit failed; no packages were changed'
    : > "$TMP/5gmodem-evidence"
    : > "$TMP/5gmodem-packages"
    while IFS="$(printf '\t')" read -r level kind name evidence; do
        case "$level" in
            BLOCK)
                # A single installed 5gmodem may own dozens of hotplug files.
                # Keep the full report on disk but display ONE family-level
                # conflict, not one conflict per file.
                case "$kind:$name:$evidence" in
                    package:*5gmodem*:*|marker:5gmodem:*|hotplug:*5gmodem*:*)
                        five_count=$((five_count + 1))
                        printf '%s\n' "$evidence" >> "$TMP/5gmodem-evidence"
                        if [ "$kind" = package ]; then
                            printf '%s\n' "$name" >> "$TMP/5gmodem-packages"
                        fi
                        continue ;;
                esac
                conflicts=$((conflicts + 1))
                say "CONFLICT: $kind $name ($evidence)"
                if [ "$kind" = package ]; then
                    if [ "$PM" = apk ]; then
                        say "  Review and remove manually if conflicting: apk del $name"
                    else
                        say "  Review and remove manually if conflicting: opkg remove $name"
                    fi
                else
                    say '  Inspect its owner, disable or remove manually with independent access.'
                fi ;;
            WARN)
                say "NOTICE: $kind $name ($evidence). Check modem port access and cron jobs." ;;
            INFO)
                say "INFO: $kind $name ($evidence)." ;;
        esac
    done < "$TMP/neighbours.audit"
    if [ "$five_count" -gt 0 ]; then
        conflicts=$((conflicts + 1))
        say "CONFLICT: 5gmodem family ($five_count evidence entries: packages, config, hotplug)."
        # Show a short summary. The audit report still has every path.
        if [ -s "$TMP/5gmodem-packages" ]; then
            five_list=$(sort -u "$TMP/5gmodem-packages" | tr '\n' ' ')
            say "  Installed: $five_list"
            if [ "$PM" = apk ]; then
                say "  Manual removal after verifying your WAN: apk del $five_list"
            else
                say "  Manual removal after verifying your WAN: opkg remove $five_list"
            fi
        fi
        say '  Examples of detected files:'
        head -n 4 "$TMP/5gmodem-evidence" | while IFS= read -r item; do
            say "    $item"
        done
        if [ "$five_count" -gt 4 ]; then
            five_more=$((five_count - 4))
            say "    ... and $five_more more detected files."
        fi
        say '  Config and hotplug remnants must be reviewed, not blindly deleted.'
    fi
    if [ "$conflicts" -gt 0 ]; then
        say "Found $conflicts competing modem families (grouped); no services or packages were changed."
        fail 'installation paused due to competing modem automation'
    fi
    say 'No confirmed competing modem orchestrators found.'
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
# GitHub /releases/latest EXCLUDES prereleases. Use the published release list,
# sort by published_at (not tag spelling/created_at) and ignore QModem,
# draft, and un-published entries. Never silently fall back to an older tag.
resolve_latest_release() {
    [ -z "$TAG" ] || { say "Explicit release requested: $TAG"; return 0; }
    api='https://api.github.com/repos/Medvedolog/medvemodem/releases?per_page=100'
    say 'Finding newest published MedveModem release (including prereleases)...'
    if ! fetch_direct "$api" "$TMP/releases.json"; then
        say 'GitHub Releases API unavailable directly'
        [ -n "${http_proxy:-}" ] || select_proxy
        fetch "$api" "$TMP/releases.json" || fail 'cannot query GitHub Releases API; refusing to guess an older version'
    fi
    # jsonfilter emits one value per release in the API array. Preserve row
    # ordering before sorting by ISO8601 UTC publication timestamp.
    jsonfilter -i "$TMP/releases.json" -e '@[*].tag_name' > "$TMP/release-tags" ||
        fail 'cannot parse release tags'
    jsonfilter -i "$TMP/releases.json" -e '@[*].published_at' > "$TMP/release-dates" ||
        fail 'cannot parse release publication dates'
    jsonfilter -i "$TMP/releases.json" -e '@[*].draft' > "$TMP/release-drafts" ||
        fail 'cannot parse release draft statuses'
    # BusyBox OpenWrt has awk but may not have paste (coreutils).
    # getline reads the corresponding fields in lockstep without extra tools.
    TAG=$(awk '
        BEGIN {
            while ((getline stamp < ARGV[1]) > 0) {
                if ((getline tag < ARGV[2]) <= 0 ||
                    (getline draft < ARGV[3]) <= 0) exit 2
                if (stamp ~ /^20[0-9][0-9]-/ &&
                    tag ~ /^v[0-9][0-9A-Za-z._-]*$/ &&
                    draft == "false" && stamp > newest) {
                    newest = stamp
                    selected = tag
                }
            }
            if (selected != "") print selected
            exit
        }
    ' "$TMP/release-dates" "$TMP/release-tags" "$TMP/release-drafts") ||
        fail 'cannot correlate GitHub release metadata'
    [ -n "$TAG" ] || fail 'no published MedveModem v* release found in GitHub API; no older-version fallback'
    say "Newest published MedveModem release: $TAG"
}
prepare_network() {
    resolve_latest_release
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
collect_running_services() {
    SERVICE_RESTORE=
    for service in medvemodem-bot medvemodemd qmodem_monitor qmodem_hc_sim qmodem_init qmodem_network; do
        if svc_running "$service"; then SERVICE_RESTORE="$SERVICE_RESTORE $service"; fi
    done
}
confirm_installation() {
    [ "$TTY" != /dev/null ] || return 0
    warning='Package upgrade may interrupt active modem connections and SSH.'
    if [ -n "$SERVICE_RESTORE" ]; then
        warning="This will temporarily stop:$SERVICE_RESTORE. SSH/mobile internet may disconnect."
    fi
    ask "Install $TAG packages ($package_files) now? $warning Continue?" no
}
quiesce_for_upgrade() {
    # Interactive users authorize service interruption in confirm_installation.
    # Noninteractive callers must explicitly opt in with --allow-disconnect.
    if [ "$TTY" = /dev/null ] && [ "$ALLOW_DISCONNECT" != 1 ]; then
        fail 'noninteractive upgrade requires --allow-disconnect and a backup management path'
    fi
    SERVICES_STOPPED=1
    for service in medvemodem-bot medvemodemd qmodem_monitor qmodem_hc_sim qmodem_init qmodem_network; do
        case " $SERVICE_RESTORE " in
            *" $service "*) stop_one "$service" ;;
        esac
    done
    wait_pids_gone medvemodemd
    wait_pids_gone medvemodem-bot
    wait_pids_gone bot-poll
    wait_pids_gone qmodem
    say 'Service stop and old-process checks completed.'
}

choose_bot() {
    case "$BOT" in yes) return 0 ;; no) return 1 ;; esac
    # Keep an already installed bot in the upgrade set automatically. Ask
    # only when this is a first-time optional bot installation.
    if installed medvemodem-bot || installed luci-app-medvemodem-bot; then
        say 'Existing Telegram bot detected; it will be upgraded together with MedveModem.'
        return 0
    fi
    ask 'Install Telegram bot and its LuCI too? (optional, first-time install)' no
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
    prepare_network
    audit_modem_owners
    show_release
    with_bot=0
    choose_bot && with_bot=1
    package_files='medvemodem luci-app-medvemodem'
    [ "$with_bot" = 0 ] || package_files="$package_files medvemodem-bot luci-app-medvemodem-bot"
    expected_version=$(printf '%s' "${TAG#v}" | sed 's/-alpha/_alpha/')
    all_current=1
    for name in $package_files; do
        current_version=$(installed_version "$name")
        if [ -z "$current_version" ] || [ "$current_version" != "$expected_version" ]; then
            all_current=0
        fi
    done
    if [ "$all_current" = 1 ] && [ "$FORCE" != 1 ]; then
        say "All selected packages already match $TAG; no package transaction is needed."
        return 0
    fi
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
    else
        apk add --simulate --allow-untrusted $files || fail 'missing/conflicting dependencies'
    fi
    collect_running_services
    if ! confirm_installation; then
        say 'Cancelled, no packages changed.'
        return 0
    fi
    quiesce_for_upgrade
    if [ "$PM" = opkg ]; then
        if [ "$FORCE" = 1 ]; then
            opkg install --force-reinstall $files || fail 'reinstallation failed'
        else
            opkg install $files || fail 'installation failed'
        fi
    else
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