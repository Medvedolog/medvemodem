#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
#
# libubox, blobmsg_json, libubus and libuci from their pinned sources into static archives that the engine links in:
# apk and ipk name these libraries after their ABI date (libubox20260213 on 25.12.5, libubox20260721 on a snapshot),
# so a build linked against an SDK's shared ones installs only on that very release (the owner's Nokia on a snapshot,
# 2026-10-27). json-c stays shared: its package is libjson-c5 on 24.10, 25.12 and snapshots alike.
#
# Usage: static-libs.sh <out-dir> <libubox-src> <ubus-src> <uci-src>
# Environment: CC, AR, CFLAGS, CPPFLAGS (the target's), JSONC_INC (the directory holding json.h).
# Writes <out-dir>/lib{ubox,blobmsg_json,ubus,uci}.a and <out-dir>/include (libubox/, libubus.h, uci.h ...).
set -eu

out=$1
ubox=$2
ubus=$3
uci=$4
: "${CC:=cc}" "${AR:=ar}" "${CFLAGS:=-Os}" "${CPPFLAGS:=}" "${JSONC_INC:=/usr/include/json-c}"

rm -rf "$out"
mkdir -p "$out/include/libubox" "$out/obj"
cp "$ubox"/*.h "$out/include/libubox/"
cp "$ubus/libubus.h" "$ubus/ubusmsg.h" "$ubus/ubus_common.h" "$out/include/"
cp "$uci/uci.h" "$uci/uci_blob.h" "$out/include/"
# uci_config.h.in holds only the two debug switches, both off as in OpenWrt's package
: > "$out/include/uci_config.h"

# the upstream projects' own flags (their CMakeLists): gnu99, no unused-parameter warnings, ubus's socket and size
flags="$CFLAGS $CPPFLAGS -std=gnu99 -Wno-unused-parameter -I$out/include -I$JSONC_INC
	-DUBUS_UNIX_SOCKET=\"/var/run/ubus/ubus.sock\" -DUBUS_MAX_MSGLEN=1048576 -DUCI_PREFIX=\"/usr\""

lib() {
	name=$1 dir=$2
	shift 2
	objs=
	for s in "$@"; do
		o="$out/obj/$name-${s%.c}.o"
		# shellcheck disable=SC2086
		$CC $flags -c "$dir/$s" -o "$o"
		objs="$objs $o"
	done
	# shellcheck disable=SC2086
	$AR rcs "$out/lib$name.a" $objs
}

lib ubox "$ubox" avl.c avl-cmp.c blob.c blobmsg.c uloop.c usock.c ustream.c ustream-fd.c vlist.c utils.c \
	safe_list.c runqueue.c md5.c kvlist.c ulog.c base64.c udebug.c udebug-remote.c
lib blobmsg_json "$ubox" blobmsg_json.c
lib ubus "$ubus" libubus.c libubus-io.c libubus-obj.c libubus-sub.c libubus-req.c libubus-acl.c
lib uci "$uci" libuci.c file.c util.c delta.c parse.c blob.c
rm -rf "$out/obj"
