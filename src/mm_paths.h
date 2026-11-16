#ifndef MEDVEMODEM_PATHS_H
#define MEDVEMODEM_PATHS_H

#ifndef SYS_USB
#define SYS_USB "/sys/bus/usb/devices"
#endif
#ifndef SYS_USB_DRIVERS
#define SYS_USB_DRIVERS "/sys/bus/usb/drivers"
#endif
#ifndef SYS_USBSERIAL_DRIVERS
#define SYS_USBSERIAL_DRIVERS "/sys/bus/usb-serial/drivers"
#endif
#ifndef MM_COMMON_SH
#define MM_COMMON_SH "/usr/share/ModemManager/modemmanager.common"
#endif
#ifndef SYS_TTY
#define SYS_TTY "/sys/class/tty"
#endif
#ifndef SYS_USBMISC
#define SYS_USBMISC "/sys/class/usbmisc"
#endif
#ifndef SYS_NET
#define SYS_NET "/sys/class/net"
#endif
#ifndef PROC_NET_ROUTE
#define PROC_NET_ROUTE "/proc/net/route"
#endif
#ifndef PROC_NET_ROUTE6
#define PROC_NET_ROUTE6 "/proc/net/ipv6_route"
#endif
#ifndef SYS_DEVICES
#define SYS_DEVICES "/sys/devices"
#endif
/* the last resort (mgr_usbrescue.c): the board's GPIO lines, the mounts and the block devices that guard the router's own
 * storage */
#ifndef SYS_GPIO
#define SYS_GPIO "/sys/class/gpio"
#endif
#ifndef PROC_MOUNTS
#define PROC_MOUNTS "/proc/mounts"
#endif
#ifndef SYS_BLOCK
#define SYS_BLOCK "/sys/block"
#endif
#ifndef USB_MODE_JSON
#define USB_MODE_JSON "/etc/usb-mode.json"
#endif
/* ModemManager's FCC unlock hooks (<vid:pid> files it runs when a modem refuses to turn its radio on) and ours,
 * linked there only by the operator's button (HANDOFF 176) */
#ifndef MM_FCC_UNLOCK_DIR
#define MM_FCC_UNLOCK_DIR "/etc/ModemManager/fcc-unlock.d"
#endif
#ifndef MM_FCC_UNLOCK_HOOK
#define MM_FCC_UNLOCK_HOOK "/usr/libexec/medvemodem/fcc-unlock-foxconn"
#endif
/* OpenWrt's usb-modeswitch package: usbmode reads usb-mode.json; usb_modeswitch where a build has the classic tool */
#ifndef USBMODE_BIN
#define USBMODE_BIN "/sbin/usbmode"
#endif
#ifndef USB_MODESWITCH_BIN
#define USB_MODESWITCH_BIN "/usr/sbin/usb_modeswitch"
#endif
#ifndef APN_DIR
#define APN_DIR "/usr/share/medvemodem/apn"
#endif
#ifndef PRESET_DIR
#define PRESET_DIR "/usr/share/medvemodem/presets"
#endif
#ifndef PROFILE_GENERIC_DIR
#define PROFILE_GENERIC_DIR "/usr/share/medvemodem/profiles/generic"
#endif
#ifndef OPENWRT_RELEASE
#define OPENWRT_RELEASE "/etc/openwrt_release"
#endif
#ifndef MM_DIAG_SALT_FILE
#define MM_DIAG_SALT_FILE "/etc/medvemodem/diag-salt"
#endif
#ifndef UCI_CONFDIR
#define UCI_CONFDIR "/etc/config"
#endif
#ifndef MM_STATE_DIR
#define MM_STATE_DIR "/etc/medvemodem"
#endif
#ifndef MM_RUN_DIR
#define MM_RUN_DIR "/var/run/medvemodem"
#endif
#ifndef MM_ESIM_BACKEND
#define MM_ESIM_BACKEND "/usr/libexec/medvemodem/esim-lpac"
#endif
#ifndef QMODEM_DIR
#define QMODEM_DIR "/usr/share/qmodem"
#endif
#ifndef DEV_DIR
#define DEV_DIR "/dev"
#endif
#ifndef NETIFD_PROTO_DIR
#define NETIFD_PROTO_DIR "/lib/netifd/proto"
#endif
#ifndef MM_RELOAD_HOOK
#define MM_RELOAD_HOOK "/usr/libexec/medvemodem/config-reload"
#endif
#ifndef MM_DIAG_DIR
#define MM_DIAG_DIR "/tmp/medvemodem/diag"
#endif

#endif
